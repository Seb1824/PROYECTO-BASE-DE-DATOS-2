#include "query/execution_trace.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace minisgbd {

ExecutionTracer::ExecutionTracer(std::string sql,
                                 std::size_t max_timeline_events,
                                 std::size_t max_car_events)
    : sql_(std::move(sql)),
      start_(Clock::now()),
      max_timeline_events_(max_timeline_events),
      max_car_events_(max_car_events) {}

int ExecutionTracer::RegisterOperator(std::string name, std::string detail,
                                      int parent_id) {
  std::lock_guard<std::mutex> lock(latch_);
  const int id = static_cast<int>(operators_.size());
  if (parent_id >= id) {
    throw std::invalid_argument(
        "El padre del operador debe registrarse primero.");
  }

  OperatorProfile profile;
  profile.id = id;
  profile.parent_id = parent_id;
  profile.name = std::move(name);
  profile.detail = std::move(detail);
  operators_.push_back(std::move(profile));
  return id;
}

void ExecutionTracer::RecordOperatorEvent(
    int operator_id, const std::string &phase, TimePoint start, TimePoint end,
    uint64_t rows_produced, std::string detail) {
  const double start_ms = MillisecondsFromStart(start);
  const double duration_ms =
      std::chrono::duration<double, std::milli>(end - start).count();

  std::lock_guard<std::mutex> lock(latch_);
  if (operator_id < 0 ||
      static_cast<std::size_t>(operator_id) >= operators_.size()) {
    return;
  }

  OperatorProfile &profile = operators_[operator_id];
  if (phase == "Open") {
    profile.open_ms += duration_ms;
    ++profile.open_calls;
  } else if (phase == "Next") {
    profile.next_ms += duration_ms;
    ++profile.next_calls;
  } else if (phase == "Close") {
    profile.close_ms += duration_ms;
    ++profile.close_calls;
  } else {
    profile.execute_ms += duration_ms;
    ++profile.execute_calls;
  }
  profile.inclusive_ms += duration_ms;
  profile.rows_out += rows_produced;

  if (timeline_.size() < max_timeline_events_) {
    TimelineEvent event;
    event.sequence = next_sequence_++;
    event.operator_id = operator_id;
    event.phase = phase;
    event.start_ms = start_ms;
    event.duration_ms = duration_ms;
    event.rows_produced = rows_produced;
    event.detail = std::move(detail);
    timeline_.push_back(std::move(event));
  } else {
    timeline_truncated_ = true;
  }
}

void ExecutionTracer::RecordCAREvent(const CAREvent &event) {
  std::lock_guard<std::mutex> lock(latch_);
  if (car_events_.size() >= max_car_events_) {
    car_events_truncated_ = true;
    return;
  }

  CARTraceEvent trace_event;
  trace_event.sequence = next_sequence_++;
  trace_event.type = event.type;
  trace_event.page_id = event.page_id;
  trace_event.frame_id = event.frame_id;
  trace_event.previous_p = event.previous_p;
  trace_event.timestamp_ms = MillisecondsFromStart(Clock::now());
  trace_event.state = event.state;
  car_events_.push_back(std::move(trace_event));
}

void ExecutionTracer::RecordCARSnapshot(const std::string &type,
                                        const CARStateSnapshot &state) {
  CAREvent event;
  event.type = type;
  event.previous_p = state.target_p;
  event.state = state;
  RecordCAREvent(event);
}

ExecutionTraceData ExecutionTracer::Finish() const {
  std::lock_guard<std::mutex> lock(latch_);

  ExecutionTraceData data;
  data.sql = sql_;
  data.operators = operators_;
  data.timeline = timeline_;
  data.car_events = car_events_;
  data.timeline_truncated = timeline_truncated_;
  data.car_events_truncated = car_events_truncated_;

  for (OperatorProfile &profile : data.operators) {
    profile.self_ms = profile.inclusive_ms;
  }
  for (const OperatorProfile &profile : data.operators) {
    if (profile.parent_id >= 0 &&
        static_cast<std::size_t>(profile.parent_id) < data.operators.size()) {
      OperatorProfile &parent = data.operators[profile.parent_id];
      parent.self_ms =
          std::max(0.0, parent.self_ms - profile.inclusive_ms);
    }
  }

  std::stable_sort(
      data.timeline.begin(), data.timeline.end(),
      [](const TimelineEvent &left, const TimelineEvent &right) {
        if (left.start_ms == right.start_ms) {
          return left.sequence < right.sequence;
        }
        return left.start_ms < right.start_ms;
      });

  return data;
}

double ExecutionTracer::MillisecondsFromStart(TimePoint point) const {
  return std::chrono::duration<double, std::milli>(point - start_).count();
}

}  // namespace minisgbd
