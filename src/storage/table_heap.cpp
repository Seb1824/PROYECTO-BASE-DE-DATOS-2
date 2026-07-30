#include "storage/table_heap.h"

#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "storage/catalog_page.h"
#include "storage/table_page.h"

namespace minisgbd {

TableHeap::TableHeap(BufferPoolManager *buffer_pool)
    : buffer_pool_(buffer_pool), catalog_(buffer_pool) {
  if (buffer_pool_ == nullptr) {
    throw std::invalid_argument(
        "TableHeap requiere un BufferPoolManager valido.");
  }

  const CatalogPage catalog_page = catalog_.Read();
  first_page_id_ = catalog_page.GetTableFirstPageId();
  last_page_id_ = catalog_page.GetTableLastPageId();
  tuple_count_ = catalog_page.GetTableTupleCount();

  if ((first_page_id_ == INVALID_PAGE_ID) !=
      (last_page_id_ == INVALID_PAGE_ID)) {
    throw std::runtime_error(
        "El catalogo contiene una cadena de tabla inconsistente.");
  }
}

bool TableHeap::Insert(const PersonRecord &person) {
  return InsertTuple(person).has_value();
}

std::optional<RID> TableHeap::InsertTuple(const PersonRecord &person) {
  ValidatePersonRecord(person);
  if (tuple_count_ == std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error(
        "La tabla alcanzo el maximo numero de registros.");
  }

  if (last_page_id_ == INVALID_PAGE_ID) {
    page_id_t new_page_id = INVALID_PAGE_ID;
    Page *page = buffer_pool_->NewPage(&new_page_id);
    if (page == nullptr) {
      return std::nullopt;
    }

    auto *table_page = reinterpret_cast<TablePage *>(page->get_data());
    table_page->Init();
    uint32_t slot = 0;
    table_page->Insert(person, &slot);
    buffer_pool_->UnpinPage(new_page_id, true);

    first_page_id_ = new_page_id;
    last_page_id_ = new_page_id;
    ++tuple_count_;
    catalog_.UpdateTable(first_page_id_, last_page_id_, tuple_count_);
    return RID{new_page_id, slot};
  }

  Page *last_page = buffer_pool_->FetchPage(last_page_id_);
  if (last_page == nullptr) {
    throw std::runtime_error(
        "No se pudo cargar la ultima pagina de la tabla.");
  }

  auto *table_page =
      reinterpret_cast<TablePage *>(last_page->get_data());
  uint32_t slot = 0;
  if (table_page->Insert(person, &slot)) {
    buffer_pool_->UnpinPage(last_page_id_, true);
    ++tuple_count_;
    catalog_.UpdateTable(first_page_id_, last_page_id_, tuple_count_);
    return RID{last_page_id_, slot};
  }

  buffer_pool_->UnpinPage(last_page_id_, false);

  page_id_t new_page_id = INVALID_PAGE_ID;
  Page *new_page = buffer_pool_->NewPage(&new_page_id);
  if (new_page == nullptr) {
    return std::nullopt;
  }

  auto *new_table_page =
      reinterpret_cast<TablePage *>(new_page->get_data());
  new_table_page->Init();
  new_table_page->Insert(person, &slot);
  buffer_pool_->UnpinPage(new_page_id, true);

  last_page = buffer_pool_->FetchPage(last_page_id_);
  if (last_page == nullptr) {
    throw std::runtime_error(
        "No se pudo enlazar la nueva pagina de tabla.");
  }
  table_page = reinterpret_cast<TablePage *>(last_page->get_data());
  table_page->SetNextPageId(new_page_id);
  buffer_pool_->UnpinPage(last_page_id_, true);

  last_page_id_ = new_page_id;
  ++tuple_count_;
  catalog_.UpdateTable(first_page_id_, last_page_id_, tuple_count_);
  return RID{new_page_id, slot};
}

bool TableHeap::RollbackInsert(const RID &rid) {
  return DeleteRecord(rid);
}

bool TableHeap::GetRecord(const RID &rid, PersonRecord *person) {
  if (!rid.IsValid() || person == nullptr ||
      rid.page_id >= buffer_pool_->GetPageCount()) {
    return false;
  }

  Page *page = buffer_pool_->FetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }
  const auto *table_page =
      reinterpret_cast<const TablePage *>(page->get_data());
  if (table_page->IsDeleted(rid.slot)) {
    buffer_pool_->UnpinPage(rid.page_id, false);
    return false;
  }

  *person = table_page->GetRecord(rid.slot);
  buffer_pool_->UnpinPage(rid.page_id, false);
  return true;
}

bool TableHeap::UpdateRecord(const RID &rid,
                             const PersonRecord &person) {
  ValidatePersonRecord(person);
  if (!rid.IsValid() ||
      rid.page_id >= buffer_pool_->GetPageCount()) {
    return false;
  }

  Page *page = buffer_pool_->FetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }
  auto *table_page = reinterpret_cast<TablePage *>(page->get_data());
  const bool updated = table_page->Update(rid.slot, person);
  buffer_pool_->UnpinPage(rid.page_id, updated);
  return updated;
}

bool TableHeap::DeleteRecord(const RID &rid) {
  if (!rid.IsValid() || rid.page_id >= buffer_pool_->GetPageCount()) {
    return false;
  }

  Page *page = buffer_pool_->FetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }
  auto *table_page = reinterpret_cast<TablePage *>(page->get_data());
  const bool deleted = table_page->Delete(rid.slot);
  buffer_pool_->UnpinPage(rid.page_id, deleted);
  if (!deleted) {
    return false;
  }

  if (tuple_count_ == 0) {
    throw std::runtime_error(
        "El catalogo contiene un conteo de tabla inconsistente.");
  }
  --tuple_count_;
  catalog_.UpdateTable(first_page_id_, last_page_id_, tuple_count_);
  return true;
}

bool TableHeap::RestoreRecord(const RID &rid,
                              const PersonRecord &person) {
  ValidatePersonRecord(person);
  if (!rid.IsValid() ||
      rid.page_id >= buffer_pool_->GetPageCount() ||
      tuple_count_ == std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  Page *page = buffer_pool_->FetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }
  auto *table_page = reinterpret_cast<TablePage *>(page->get_data());
  const bool restored = table_page->Restore(rid.slot, person);
  buffer_pool_->UnpinPage(rid.page_id, restored);
  if (!restored) {
    return false;
  }

  ++tuple_count_;
  catalog_.UpdateTable(first_page_id_, last_page_id_, tuple_count_);
  return true;
}

std::vector<LocatedRecord> TableHeap::ReadAll() {
  std::vector<LocatedRecord> records;
  records.reserve(tuple_count_);
  std::unordered_set<page_id_t> visited_pages;
  page_id_t page_id = first_page_id_;

  while (page_id != INVALID_PAGE_ID) {
    if (page_id < 0 || page_id >= buffer_pool_->GetPageCount() ||
        !visited_pages.insert(page_id).second) {
      throw std::runtime_error(
          "La cadena de paginas de TableHeap esta corrupta.");
    }

    Page *page = buffer_pool_->FetchPage(page_id);
    if (page == nullptr) {
      throw std::runtime_error(
          "No se pudo leer una pagina de TableHeap.");
    }
    const auto *table_page =
        reinterpret_cast<const TablePage *>(page->get_data());
    for (uint32_t slot = 0; slot < table_page->GetSize(); ++slot) {
      if (!table_page->IsDeleted(slot)) {
        const PersonRecord person = table_page->GetRecord(slot);
        records.push_back(LocatedRecord{
            RID{page_id, slot}, person});
      }
    }
    const page_id_t next_page_id = table_page->GetNextPageId();
    buffer_pool_->UnpinPage(page_id, false);
    page_id = next_page_id;
  }

  if (records.size() != tuple_count_) {
    throw std::runtime_error(
        "El conteo del catalogo no coincide con TableHeap.");
  }
  return records;
}

page_id_t TableHeap::GetFirstPageId() const {
  return first_page_id_;
}

page_id_t TableHeap::GetLastPageId() const {
  return last_page_id_;
}

uint32_t TableHeap::GetTupleCount() const {
  return tuple_count_;
}

BufferPoolManager *TableHeap::GetBufferPool() const {
  return buffer_pool_;
}

}  // namespace minisgbd
