#include "query/projection_operator.h"

#include <stdexcept>

namespace minisgbd {

ProjectionOperator::ProjectionOperator(Operator *child, bool include_key,
                                       bool include_value)
    : child_(child),
      include_key_(include_key),
      include_value_(include_value) {
  if (child_ == nullptr) {
    throw std::invalid_argument(
        "ProjectionOperator requiere un operador hijo valido.");
  }
  if (!include_key_ && !include_value_) {
    throw std::invalid_argument(
        "ProjectionOperator requiere al menos una columna.");
  }
}

void ProjectionOperator::Open() {
  child_->Open();
  initialized_ = true;
}

bool ProjectionOperator::Next(Tuple *tuple) {
  if (!initialized_ || tuple == nullptr) {
    return false;
  }

  Tuple source;
  if (!child_->Next(&source)) {
    return false;
  }
  if (!include_key_) {
    source.key = 0;
  }
  if (!include_value_) {
    source.value = 0;
  }
  *tuple = source;
  return true;
}

void ProjectionOperator::Close() {
  if (initialized_) {
    child_->Close();
  }
  initialized_ = false;
}

}  // namespace minisgbd
