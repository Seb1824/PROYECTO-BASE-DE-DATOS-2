#include "query/seq_scan_operator.h"

#include <stdexcept>

#include "storage/table_page.h"

namespace minisgbd {

SeqScanOperator::SeqScanOperator(const std::vector<Tuple> &tuples)
    : tuples_(&tuples) {}

SeqScanOperator::SeqScanOperator(TableHeap *table_heap)
    : table_heap_(table_heap) {
  if (table_heap_ == nullptr) {
    throw std::invalid_argument(
        "SeqScanOperator requiere un TableHeap valido.");
  }
}

SeqScanOperator::~SeqScanOperator() {
  DoClose();
}

void SeqScanOperator::DoOpen() {
  if (initialized_) {
    DoClose();
  }

  cursor_ = 0;
  initialized_ = true;

  if (table_heap_ != nullptr) {
    LoadPhysicalPage(table_heap_->GetFirstPageId());
  }
}

bool SeqScanOperator::DoNext(Tuple *tuple) {
  if (!initialized_ || tuple == nullptr) {
    return false;
  }

  if (tuples_ != nullptr) {
    if (cursor_ >= tuples_->size()) {
      return false;
    }

    *tuple = (*tuples_)[cursor_];
    ++cursor_;
    return true;
  }

  while (current_page_ != nullptr) {
    const auto *table_page =
        reinterpret_cast<const TablePage *>(current_page_->get_data());

    if (cursor_ < table_page->GetSize()) {
      const uint32_t slot = static_cast<uint32_t>(cursor_);
      ++cursor_;
      if (table_page->IsDeleted(slot)) {
        continue;
      }
      *tuple = table_page->GetRecord(slot);
      return true;
    }

    const page_id_t next_page_id = table_page->GetNextPageId();
    table_heap_->GetBufferPool()->UnpinPage(current_page_id_, false);
    current_page_ = nullptr;
    current_page_id_ = INVALID_PAGE_ID;
    cursor_ = 0;
    LoadPhysicalPage(next_page_id);
  }

  return false;
}

void SeqScanOperator::DoClose() {
  if (current_page_ != nullptr && table_heap_ != nullptr) {
    table_heap_->GetBufferPool()->UnpinPage(current_page_id_, false);
  }

  current_page_ = nullptr;
  current_page_id_ = INVALID_PAGE_ID;
  initialized_ = false;
  cursor_ = 0;
}

void SeqScanOperator::LoadPhysicalPage(page_id_t page_id) {
  if (page_id == INVALID_PAGE_ID) {
    return;
  }

  current_page_ = table_heap_->GetBufferPool()->FetchPage(page_id);
  if (current_page_ == nullptr) {
    throw std::runtime_error(
        "SeqScanOperator no pudo cargar una pagina fisica.");
  }
  current_page_id_ = page_id;
}

}  // namespace minisgbd
