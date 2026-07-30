#pragma once

#include "query/tuple.h"

namespace minisgbd {

class Operator {
 public:
  virtual ~Operator() = default;

  virtual void Open() = 0;
  virtual bool Next(Tuple *tuple) = 0;
  virtual void Close() = 0;
};

}  // namespace minisgbd
