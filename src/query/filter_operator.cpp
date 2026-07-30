#include "query/filter_operator.h"

#include <stdexcept>
#include <utility>

namespace minisgbd {

FilterOperator::FilterOperator(Operator *child, Predicate predicate)
    : child_(child), predicate_(std::move(predicate)) {
  if (child_ == nullptr) {
    throw std::invalid_argument(
        "FilterOperator requiere un operador hijo valido.");
  }

  if (!predicate_) {
    throw std::invalid_argument(
        "FilterOperator requiere un predicado valido.");
  }
}

void FilterOperator::DoOpen() {
  child_->Open();
  initialized_ = true;
}

bool FilterOperator::DoNext(Tuple *tuple) {
  if (!initialized_ || tuple == nullptr) {
    return false;
  }

  Tuple candidate;
  while (child_->Next(&candidate)) {
    if (predicate_(candidate)) {
      *tuple = candidate;
      return true;
    }
  }

  return false;
}

void FilterOperator::DoClose() {
  if (initialized_) {
    child_->Close();
  }

  initialized_ = false;
}

}  // namespace minisgbd
