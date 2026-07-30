#include "storage/table_heap.h"

#include <limits>
#include <stdexcept>

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

bool TableHeap::Insert(int key, int value) {
  if (tuple_count_ == std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error(
        "La tabla alcanzo el maximo numero de registros.");
  }

  if (last_page_id_ == INVALID_PAGE_ID) {
    page_id_t new_page_id = INVALID_PAGE_ID;
    Page *page = buffer_pool_->NewPage(&new_page_id);
    if (page == nullptr) {
      return false;
    }

    auto *table_page = reinterpret_cast<TablePage *>(page->get_data());
    table_page->Init();
    table_page->Insert(key, value);
    buffer_pool_->UnpinPage(new_page_id, true);

    first_page_id_ = new_page_id;
    last_page_id_ = new_page_id;
    ++tuple_count_;
    catalog_.UpdateTable(first_page_id_, last_page_id_, tuple_count_);
    return true;
  }

  Page *last_page = buffer_pool_->FetchPage(last_page_id_);
  if (last_page == nullptr) {
    throw std::runtime_error(
        "No se pudo cargar la ultima pagina de la tabla.");
  }

  auto *table_page =
      reinterpret_cast<TablePage *>(last_page->get_data());
  if (table_page->Insert(key, value)) {
    buffer_pool_->UnpinPage(last_page_id_, true);
    ++tuple_count_;
    catalog_.UpdateTable(first_page_id_, last_page_id_, tuple_count_);
    return true;
  }

  buffer_pool_->UnpinPage(last_page_id_, false);

  page_id_t new_page_id = INVALID_PAGE_ID;
  Page *new_page = buffer_pool_->NewPage(&new_page_id);
  if (new_page == nullptr) {
    return false;
  }

  auto *new_table_page =
      reinterpret_cast<TablePage *>(new_page->get_data());
  new_table_page->Init();
  new_table_page->Insert(key, value);
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
  return true;
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
