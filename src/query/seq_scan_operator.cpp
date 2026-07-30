#include "query/seq_scan_operator.h"

namespace minisgbd {

SeqScanOperator::SeqScanOperator(const std::vector<Tuple> &tuples)
    : tuples_(tuples) {}

void SeqScanOperator::Open() {
  cursor_ = 0;
  initialized_ = true;
}

bool SeqScanOperator::Next(Tuple *tuple) {
  if (!initialized_ || tuple == nullptr || cursor_ >= tuples_.size()) {
    return false;
  }

  *tuple = tuples_[cursor_];
  ++cursor_;
  return true;
}

void SeqScanOperator::Close() {
  initialized_ = false;
  cursor_ = 0;
}

}  // namespace minisgbd
