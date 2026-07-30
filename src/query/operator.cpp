#include "query/operator.h"

#include <exception>

namespace minisgbd {

void Operator::Open() {
  const auto start = ExecutionTracer::Clock::now();
  try {
    DoOpen();
    if (tracer_ != nullptr) {
      tracer_->RecordOperatorEvent(operator_id_, "Open", start,
                                   ExecutionTracer::Clock::now());
    }
  } catch (const std::exception &error) {
    if (tracer_ != nullptr) {
      tracer_->RecordOperatorEvent(operator_id_, "Open", start,
                                   ExecutionTracer::Clock::now(), 0,
                                   error.what());
    }
    throw;
  }
}

bool Operator::Next(Tuple *tuple) {
  const auto start = ExecutionTracer::Clock::now();
  try {
    const bool produced = DoNext(tuple);
    if (tracer_ != nullptr) {
      tracer_->RecordOperatorEvent(operator_id_, "Next", start,
                                   ExecutionTracer::Clock::now(),
                                   produced ? 1 : 0);
    }
    return produced;
  } catch (const std::exception &error) {
    if (tracer_ != nullptr) {
      tracer_->RecordOperatorEvent(operator_id_, "Next", start,
                                   ExecutionTracer::Clock::now(), 0,
                                   error.what());
    }
    throw;
  }
}

void Operator::Close() {
  const auto start = ExecutionTracer::Clock::now();
  try {
    DoClose();
    if (tracer_ != nullptr) {
      tracer_->RecordOperatorEvent(operator_id_, "Close", start,
                                   ExecutionTracer::Clock::now());
    }
  } catch (const std::exception &error) {
    if (tracer_ != nullptr) {
      tracer_->RecordOperatorEvent(operator_id_, "Close", start,
                                   ExecutionTracer::Clock::now(), 0,
                                   error.what());
    }
    throw;
  }
}

void Operator::AttachTrace(ExecutionTracer *tracer, int operator_id) {
  tracer_ = tracer;
  operator_id_ = operator_id;
}

}  // namespace minisgbd
