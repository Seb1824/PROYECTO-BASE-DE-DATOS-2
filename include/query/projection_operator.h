#pragma once

#include <string>
#include <vector>

#include "query/operator.h"

namespace minisgbd {

class ProjectionOperator : public Operator {
 public:
  ProjectionOperator(Operator *child, std::vector<std::string> columns);
  ~ProjectionOperator() override = default;

 protected:
  void DoOpen() override;
  bool DoNext(Tuple *tuple) override;
  void DoClose() override;

 private:
  Operator *child_;
  std::vector<std::string> columns_;
  bool initialized_{false};
};

}  // namespace minisgbd
