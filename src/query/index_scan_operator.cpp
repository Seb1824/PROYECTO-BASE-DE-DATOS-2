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

void IndexScanOperator::Open() {
  cursor_ = 0;
  results_.clear();

  int encoded_rid = 0;
  if (hash_index_->GetValue(search_key_, &encoded_rid)) {
    const RID rid = RID::Decode(encoded_rid);
    int key = 0;
    int value = 0;
    if (!table_heap_->GetRecord(rid, &key, &value) || key != search_key_) {
      throw std::runtime_error(
          "El indice contiene un RID inexistente o inconsistente.");
    }
    results_.push_back(Tuple{key, value});
  }

  initialized_ = true;
}

bool IndexScanOperator::Next(Tuple *tuple) {
  if (!initialized_ || tuple == nullptr || cursor_ >= results_.size()) {
    return false;
  }

  *tuple = results_[cursor_];
  ++cursor_;
  return true;
}

void IndexScanOperator::Close() {
  initialized_ = false;
  results_.clear();
  cursor_ = 0;
}

}  // namespace minisgbd
