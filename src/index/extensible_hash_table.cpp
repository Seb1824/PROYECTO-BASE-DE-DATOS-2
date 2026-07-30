#include "index/extensible_hash_table.h"

#include <functional>
#include <stdexcept>
#include <vector>

#include "storage/catalog_manager.h"
#include "storage/catalog_page.h"

namespace minisgbd {

ExtensibleHashTable::ExtensibleHashTable(BufferPoolManager *bpm) : bpm_(bpm) {
  if (bpm_ == nullptr) {
    throw std::invalid_argument(
        "ExtensibleHashTable requiere un BufferPoolManager valido.");
  }

  CatalogManager catalog(bpm_);
  const CatalogPage catalog_page = catalog.Read();
  directory_page_id_ = catalog_page.GetIndexDirectoryPageId();

  if (directory_page_id_ != INVALID_PAGE_ID) {
    if (directory_page_id_ >= bpm_->GetPageCount()) {
      throw std::runtime_error(
          "El catalogo contiene una raiz de indice invalida.");
    }
    return;
  }

  Page *dir_page = bpm_->NewPage(&directory_page_id_);
  if (dir_page == nullptr) {
    throw std::runtime_error(
        "No se pudo crear el directorio del indice.");
  }
  auto *dir =
      reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());
  dir->Init(0);
  bpm_->UnpinPage(directory_page_id_, true);

  page_id_t bucket_page_id = INVALID_PAGE_ID;
  Page *bucket_page = bpm_->NewPage(&bucket_page_id);
  if (bucket_page == nullptr) {
    throw std::runtime_error(
        "No se pudo crear el bucket inicial del indice.");
  }
  auto *bucket =
      reinterpret_cast<HashBucketPage *>(bucket_page->get_data());
  bucket->Init(0);
  bpm_->UnpinPage(bucket_page_id, true);

  dir_page = bpm_->FetchPage(directory_page_id_);
  if (dir_page == nullptr) {
    throw std::runtime_error(
        "No se pudo actualizar el directorio inicial.");
  }
  dir = reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());
  dir->SetBucketPageId(0, bucket_page_id);
  bpm_->UnpinPage(directory_page_id_, true);

  catalog.SetIndexDirectoryPageId(directory_page_id_);
  newly_created_ = true;
}

uint32_t ExtensibleHashTable::Hash(KeyType key) const {
  return std::hash<KeyType>()(key);
}

uint32_t ExtensibleHashTable::GetBucketIndex(KeyType key) const {
  Page *dir_page = bpm_->FetchPage(directory_page_id_);
  if (dir_page == nullptr) {
    throw std::runtime_error(
        "No se pudo cargar el directorio del indice.");
  }
  auto *dir =
      reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());

  uint32_t hash_val = Hash(key);
  uint32_t mask = (1U << dir->GetGlobalDepth()) - 1U;
  uint32_t bucket_idx = hash_val & mask;

  bpm_->UnpinPage(directory_page_id_, false);
  return bucket_idx;
}

bool ExtensibleHashTable::GetValue(KeyType key, ValueType *result) {
  if (result == nullptr) {
    return false;
  }

  uint32_t bucket_idx = GetBucketIndex(key);

  Page *dir_page = bpm_->FetchPage(directory_page_id_);
  if (dir_page == nullptr) {
    throw std::runtime_error(
        "No se pudo cargar el directorio del indice.");
  }
  auto *dir =
      reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());
  page_id_t bucket_page_id = dir->GetBucketPageId(bucket_idx);
  bpm_->UnpinPage(directory_page_id_, false);

  Page *bucket_page = bpm_->FetchPage(bucket_page_id);
  if (bucket_page == nullptr) {
    throw std::runtime_error(
        "No se pudo cargar un bucket del indice.");
  }
  auto *bucket =
      reinterpret_cast<HashBucketPage *>(bucket_page->get_data());

  bool found = bucket->GetValue(key, result);
  bpm_->UnpinPage(bucket_page_id, false);

  return found;
}

bool ExtensibleHashTable::Insert(KeyType key, ValueType value) {
  ValueType existing;
  if (GetValue(key, &existing)) {
    return false; 
  }
  uint32_t bucket_idx = GetBucketIndex(key);

  Page *dir_page = bpm_->FetchPage(directory_page_id_);
  if (dir_page == nullptr) {
    throw std::runtime_error(
        "No se pudo cargar el directorio para insertar.");
  }
  auto *dir =
      reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());
  page_id_t bucket_page_id = dir->GetBucketPageId(bucket_idx);

  Page *bucket_page = bpm_->FetchPage(bucket_page_id);
  if (bucket_page == nullptr) {
    bpm_->UnpinPage(directory_page_id_, false);
    throw std::runtime_error(
        "No se pudo cargar el bucket para insertar.");
  }
  auto *bucket =
      reinterpret_cast<HashBucketPage *>(bucket_page->get_data());

  if (bucket->Insert(key, value)) {
    bpm_->UnpinPage(bucket_page_id, true);
    bpm_->UnpinPage(directory_page_id_, false);
    return true;
  }

  int local_depth = bucket->GetLocalDepth();

  if (local_depth == dir->GetGlobalDepth()) {
    int global_depth = dir->GetGlobalDepth();
    const uint32_t expanded_size = 1U << (global_depth + 1);
    if (expanded_size >
        static_cast<uint32_t>(DIRECTORY_ARRAY_SIZE)) {
      bpm_->UnpinPage(bucket_page_id, false);
      bpm_->UnpinPage(directory_page_id_, false);
      throw std::overflow_error(
          "El directorio hash alcanzo la capacidad de una pagina.");
    }
    dir->SetGlobalDepth(global_depth + 1);
    int current_dir_size = 1 << global_depth;
    for (int i = 0; i < current_dir_size; i++) {
      dir->SetBucketPageId(i + current_dir_size, dir->GetBucketPageId(i));
    }
  }

  page_id_t new_bucket_page_id;
  Page *new_bucket_page = bpm_->NewPage(&new_bucket_page_id);
  if (new_bucket_page == nullptr) {
    bpm_->UnpinPage(bucket_page_id, false);
    bpm_->UnpinPage(directory_page_id_, false);
    return false;
  }
  auto *new_bucket =
      reinterpret_cast<HashBucketPage *>(new_bucket_page->get_data());

  bucket->SetLocalDepth(local_depth + 1);
  new_bucket->Init(local_depth + 1);

  std::vector<std::pair<KeyType, ValueType>> temp_entries;
  for (int i = 0; i < bucket->GetSize(); i++) {
    temp_entries.push_back({bucket->KeyAt(i), bucket->ValueAt(i)});
  }
  bucket->Clear();

  uint32_t local_mask = (1 << bucket->GetLocalDepth()) - 1;
  for (const auto &entry : temp_entries) {
    uint32_t hash_val = Hash(entry.first);
    if ((hash_val & local_mask) == (bucket_idx & local_mask)) {
      bucket->Insert(entry.first, entry.second);
    } else {
      new_bucket->Insert(entry.first, entry.second);
    }
  }

  int dir_size = 1 << dir->GetGlobalDepth();
  for (int i = 0; i < dir_size; i++) {
    if (dir->GetBucketPageId(i) == bucket_page_id) {
      if ((i & local_mask) != (bucket_idx & local_mask)) {
        dir->SetBucketPageId(i, new_bucket_page_id);
      }
    }
  }

  bpm_->UnpinPage(bucket_page_id, true);
  bpm_->UnpinPage(new_bucket_page_id, true);
  bpm_->UnpinPage(directory_page_id_, true);

  return Insert(key, value);
}

page_id_t ExtensibleHashTable::GetDirectoryPageId() const {
  return directory_page_id_;
}

bool ExtensibleHashTable::IsNewlyCreated() const {
  return newly_created_;
}

}  // namespace minisgbd
