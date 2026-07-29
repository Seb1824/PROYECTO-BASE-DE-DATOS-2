#include "index/extensible_hash_table.h"
#include <functional>
#include <vector>

namespace minisgbd {

ExtensibleHashTable::ExtensibleHashTable(BufferPoolManager *bpm) : bpm_(bpm) {
  Page *dir_page = bpm_->NewPage(&directory_page_id_);
  HashDirectoryPage *dir = reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());
  dir->Init(0);

  page_id_t bucket_page_id;
  Page *bucket_page = bpm_->NewPage(&bucket_page_id);
  HashBucketPage *bucket = reinterpret_cast<HashBucketPage *>(bucket_page->get_data());
  bucket->Init(0);

  dir->SetBucketPageId(0, bucket_page_id);

  bpm_->UnpinPage(bucket_page_id, true);
  bpm_->UnpinPage(directory_page_id_, true);
}

uint32_t ExtensibleHashTable::Hash(KeyType key) const {
  return std::hash<KeyType>()(key);
}

uint32_t ExtensibleHashTable::GetBucketIndex(KeyType key) const {
  Page *dir_page = bpm_->FetchPage(directory_page_id_);
  HashDirectoryPage *dir = reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());

  uint32_t hash_val = Hash(key);
  uint32_t mask = (1 << dir->GetGlobalDepth()) - 1;
  uint32_t bucket_idx = hash_val & mask;

  bpm_->UnpinPage(directory_page_id_, false);
  return bucket_idx;
}

bool ExtensibleHashTable::GetValue(KeyType key, ValueType *result) {
  uint32_t bucket_idx = GetBucketIndex(key);

  Page *dir_page = bpm_->FetchPage(directory_page_id_);
  HashDirectoryPage *dir = reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());
  page_id_t bucket_page_id = dir->GetBucketPageId(bucket_idx);
  bpm_->UnpinPage(directory_page_id_, false);

  Page *bucket_page = bpm_->FetchPage(bucket_page_id);
  HashBucketPage *bucket = reinterpret_cast<HashBucketPage *>(bucket_page->get_data());

  bool found = bucket->GetValue(key, result);
  bpm_->UnpinPage(bucket_page_id, false);

  return found;
}

bool ExtensibleHashTable::Insert(KeyType key, ValueType value) {
  uint32_t bucket_idx = GetBucketIndex(key);

  Page *dir_page = bpm_->FetchPage(directory_page_id_);
  HashDirectoryPage *dir = reinterpret_cast<HashDirectoryPage *>(dir_page->get_data());
  page_id_t bucket_page_id = dir->GetBucketPageId(bucket_idx);

  Page *bucket_page = bpm_->FetchPage(bucket_page_id);
  HashBucketPage *bucket = reinterpret_cast<HashBucketPage *>(bucket_page->get_data());

  if (bucket->Insert(key, value)) {
    bpm_->UnpinPage(bucket_page_id, true);
    bpm_->UnpinPage(directory_page_id_, false);
    return true;
  }

  int local_depth = bucket->GetLocalDepth();

  if (local_depth == dir->GetGlobalDepth()) {
    int global_depth = dir->GetGlobalDepth();
    dir->SetGlobalDepth(global_depth + 1);
    int current_dir_size = 1 << global_depth;
    for (int i = 0; i < current_dir_size; i++) {
      dir->SetBucketPageId(i + current_dir_size, dir->GetBucketPageId(i));
    }
  }

  page_id_t new_bucket_page_id;
  Page *new_bucket_page = bpm_->NewPage(&new_bucket_page_id);
  HashBucketPage *new_bucket = reinterpret_cast<HashBucketPage *>(new_bucket_page->get_data());

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

} // namespace minisgbd