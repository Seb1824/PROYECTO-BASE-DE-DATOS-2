#include "query/index_scan_operator.h"

namespace minisgbd {

IndexScanOperator::IndexScanOperator(ExtensibleHashTable *hash_index,
                                     int search_key)
    : hash_index_(hash_index), search_key_(search_key) {}

void IndexScanOperator::Open() {
  cursor_ = 0;
  results_.clear();

  int found_value = 0;
  if (hash_index_->GetValue(search_key_, &found_value)) {
    results_.push_back(Tuple{search_key_, found_value});
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
