#pragma once

#include <string>

#include "query/execution_trace.h"
#include "query/tuple.h"

namespace minisgbd {

class Operator {
 public:
  virtual ~Operator() = default;

  void Open();
  bool Next(Tuple *tuple);
  void Close();

  void AttachTrace(ExecutionTracer *tracer, int operator_id);

 protected:
  virtual void DoOpen() = 0;
  virtual bool DoNext(Tuple *tuple) = 0;
  virtual void DoClose() = 0;

 private:
  ExecutionTracer *tracer_{nullptr};
  int operator_id_{-1};
};

}  // namespace minisgbd
