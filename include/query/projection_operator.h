#pragma once

#include "query/operator.h"

namespace minisgbd {

class ProjectionOperator : public Operator {
 public:
  ProjectionOperator(Operator *child, bool include_key, bool include_value);
  ~ProjectionOperator() override = default;

  void Open() override;
  bool Next(Tuple *tuple) override;
  void Close() override;

 private:
  Operator *child_;
  bool include_key_;
  bool include_value_;
  bool initialized_{false};
};

}  // namespace minisgbd
