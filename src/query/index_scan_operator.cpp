#include "query/index_scan_operator.h"

#include <stdexcept>

namespace minisgbd {

IndexScanOperator::IndexScanOperator(ExtensibleHashTable *hash_index,
                                     TableHeap *table_heap, int search_key)
    : hash_index_(hash_index),
      table_heap_(table_heap),
      search_key_(search_key) {
  if (hash_index_ == nullptr || table_heap_ == nullptr) {
    throw std::invalid_argument(
        "IndexScanOperator requiere indice y TableHeap validos.");
  }
}

void IndexScanOperator::DoOpen() {
  cursor_ = 0;
  results_.clear();

  int encoded_rid = 0;
  if (hash_index_->GetValue(search_key_, &encoded_rid)) {
    const RID rid = RID::Decode(encoded_rid);
    PersonRecord person;
    if (!table_heap_->GetRecord(rid, &person) ||
        person.id != search_key_) {
      throw std::runtime_error(
          "El indice contiene un RID inexistente o inconsistente.");
    }
    results_.push_back(std::move(person));
  }

  initialized_ = true;
}

bool IndexScanOperator::DoNext(Tuple *tuple) {
  if (!initialized_ || tuple == nullptr || cursor_ >= results_.size()) {
    return false;
  }

  *tuple = results_[cursor_];
  ++cursor_;
  return true;
}

void IndexScanOperator::DoClose() {
  initialized_ = false;
  results_.clear();
  cursor_ = 0;
}

}  // namespace minisgbd
