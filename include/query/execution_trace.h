#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "buffer/car_replacer.h"

namespace minisgbd {

struct OperatorProfile {
  int id{-1};
  int parent_id{-1};
  std::string name;
  std::string detail;
  double open_ms{0.0};
  double next_ms{0.0};
  double close_ms{0.0};
  double execute_ms{0.0};
  double inclusive_ms{0.0};
  double self_ms{0.0};
  uint64_t open_calls{0};
  uint64_t next_calls{0};
  uint64_t close_calls{0};
  uint64_t execute_calls{0};
  uint64_t rows_out{0};
};

struct TimelineEvent {
  std::size_t sequence{0};
  int operator_id{-1};
  std::string category{"operator"};
  std::string phase;
  double start_ms{0.0};
  double duration_ms{0.0};
  uint64_t rows_produced{0};
  std::string detail;
};

struct CARTraceEvent {
  std::size_t sequence{0};
  std::string type;
  page_id_t page_id{INVALID_PAGE_ID};
  frame_id_t frame_id{INVALID_FRAME_ID};
  double previous_p{0.0};
  double timestamp_ms{0.0};
  CARStateSnapshot state;
};

struct ExecutionTraceData {
  std::string sql;
  std::vector<OperatorProfile> operators;
  std::vector<TimelineEvent> timeline;
  std::vector<CARTraceEvent> car_events;
  bool timeline_truncated{false};
  bool car_events_truncated{false};
};

class ExecutionTracer {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit ExecutionTracer(std::string sql,
                           std::size_t max_timeline_events = 2000,
                           std::size_t max_car_events = 1000);

  int RegisterOperator(std::string name, std::string detail,
                       int parent_id = -1);

  void RecordOperatorEvent(int operator_id, const std::string &phase,
                           TimePoint start, TimePoint end,
                           uint64_t rows_produced = 0,
                           std::string detail = "");

  void RecordCAREvent(const CAREvent &event);

  void RecordCARSnapshot(const std::string &type,
                         const CARStateSnapshot &state);

  ExecutionTraceData Finish() const;

 private:
  double MillisecondsFromStart(TimePoint point) const;

  std::string sql_;
  TimePoint start_;
  std::size_t max_timeline_events_;
  std::size_t max_car_events_;
  std::vector<OperatorProfile> operators_;
  std::vector<TimelineEvent> timeline_;
  std::vector<CARTraceEvent> car_events_;
  std::size_t next_sequence_{0};
  bool timeline_truncated_{false};
  bool car_events_truncated_{false};
  mutable std::mutex latch_;
};

}  // namespace minisgbd
