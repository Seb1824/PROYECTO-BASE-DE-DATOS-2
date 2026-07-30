#include "query/query_profiler.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace minisgbd {
namespace {

uint64_t CounterDelta(uint64_t before, uint64_t after) {
  return after >= before ? after - before : after;
}

}  // namespace

QueryProfiler::QueryProfiler(QueryExecutor *executor,
                             BufferPoolManager *buffer_pool,
                             DiskManager *disk_manager)
    : executor_(executor),
      buffer_pool_(buffer_pool),
      disk_manager_(disk_manager) {
  if (executor_ == nullptr) {
    throw std::invalid_argument(
        "QueryProfiler requiere un QueryExecutor valido.");
  }
}

ProfiledQueryResult QueryProfiler::Execute(const std::string &sql) {
  const uint64_t hits_before =
      buffer_pool_ != nullptr ? buffer_pool_->GetHitCount() : 0;
  const uint64_t misses_before =
      buffer_pool_ != nullptr ? buffer_pool_->GetMissCount() : 0;
  const uint64_t reads_before =
      disk_manager_ != nullptr ? disk_manager_->GetReadCount() : 0;
  const uint64_t writes_before =
      disk_manager_ != nullptr ? disk_manager_->GetWriteCount() : 0;

  const auto start = std::chrono::steady_clock::now();
  std::vector<Tuple> rows = executor_->Execute(sql);
  const auto end = std::chrono::steady_clock::now();

  const uint64_t hits_after =
      buffer_pool_ != nullptr ? buffer_pool_->GetHitCount() : 0;
  const uint64_t misses_after =
      buffer_pool_ != nullptr ? buffer_pool_->GetMissCount() : 0;
  const uint64_t reads_after =
      disk_manager_ != nullptr ? disk_manager_->GetReadCount() : 0;
  const uint64_t writes_after =
      disk_manager_ != nullptr ? disk_manager_->GetWriteCount() : 0;

  QueryMetrics metrics;
  metrics.elapsed_ms =
      std::chrono::duration<double, std::milli>(end - start).count();
  metrics.buffer_hits = CounterDelta(hits_before, hits_after);
  metrics.buffer_misses = CounterDelta(misses_before, misses_after);
  metrics.disk_reads = CounterDelta(reads_before, reads_after);
  metrics.disk_writes = CounterDelta(writes_before, writes_after);
  metrics.io_operations = metrics.disk_reads + metrics.disk_writes;

  const uint64_t buffer_accesses =
      metrics.buffer_hits + metrics.buffer_misses;
  if (buffer_accesses != 0) {
    metrics.buffer_hit_ratio =
        static_cast<double>(metrics.buffer_hits) /
        static_cast<double>(buffer_accesses);
  }

  ProfiledQueryResult result;
  result.rows = std::move(rows);
  result.plan_type = executor_->GetLastPlanType();
  result.metrics = metrics;
  return result;
}

}  // namespace minisgbd
