#include "query/projection_operator.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace minisgbd {

ProjectionOperator::ProjectionOperator(
    Operator *child, std::vector<std::string> columns)
    : child_(child),
      columns_(std::move(columns)) {
  if (child_ == nullptr) {
    throw std::invalid_argument(
        "ProjectionOperator requiere un operador hijo valido.");
  }
  if (columns_.empty()) {
    throw std::invalid_argument(
        "ProjectionOperator requiere al menos una columna.");
  }
}

void ProjectionOperator::DoOpen() {
  child_->Open();
  initialized_ = true;
}

bool ProjectionOperator::DoNext(Tuple *tuple) {
  if (!initialized_ || tuple == nullptr) {
    return false;
  }

  Tuple source;
  if (!child_->Next(&source)) {
    return false;
  }
  const auto includes = [this](const std::string &column) {
    return std::find(columns_.begin(), columns_.end(), column) !=
           columns_.end();
  };
  if (!includes("id")) {
    source.id = 0;
  }
  if (!includes("nombre")) {
    source.nombre.clear();
  }
  if (!includes("ciudad")) {
    source.ciudad.clear();
  }
  if (!includes("profesion")) {
    source.profesion.clear();
  }
  *tuple = source;
  return true;
}

void ProjectionOperator::DoClose() {
  if (initialized_) {
    child_->Close();
  }
  initialized_ = false;
}

}  // namespace minisgbd
