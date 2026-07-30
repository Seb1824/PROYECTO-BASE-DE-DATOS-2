#pragma once

#include <functional>

#include "query/operator.h"

namespace minisgbd {

class FilterOperator : public Operator {
 public:
  using Predicate = std::function<bool(const Tuple &)>;

  FilterOperator(Operator *child, Predicate predicate);
  ~FilterOperator() override = default;

 protected:
  void DoOpen() override;
  bool DoNext(Tuple *tuple) override;
  void DoClose() override;

 private:
  Operator *child_;
  Predicate predicate_;
  bool initialized_{false};
};

}  // namespace minisgbd
