#pragma once

#include <cstddef>
#include <vector>

#include "query/operator.h"

namespace minisgbd {

class SeqScanOperator : public Operator {
 public:
  explicit SeqScanOperator(const std::vector<Tuple> &tuples);
  ~SeqScanOperator() override = default;

  void Open() override;
  bool Next(Tuple *tuple) override;
  void Close() override;

 private:
  const std::vector<Tuple> &tuples_;
  std::size_t cursor_{0};
  bool initialized_{false};
};

}  // namespace minisgbd
