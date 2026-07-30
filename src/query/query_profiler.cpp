#include "query/query_profiler.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>

#include "query/query_visualizer.h"

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

  std::unique_ptr<ExecutionTracer> tracer;
  CARReplacer *replacer = nullptr;
  if (tracing_enabled_) {
    tracer = std::make_unique<ExecutionTracer>(sql);
    replacer =
        buffer_pool_ != nullptr ? buffer_pool_->GetReplacer() : nullptr;
    if (replacer != nullptr) {
      tracer->RecordCARSnapshot("INICIO", replacer->GetSnapshot());
      ExecutionTracer *active_tracer = tracer.get();
      replacer->SetEventObserver(
          [active_tracer](const CAREvent &event) {
            active_tracer->RecordCAREvent(event);
          });
    }
    executor_->SetExecutionTracer(tracer.get());
  }

  const auto start = std::chrono::steady_clock::now();
  std::vector<Tuple> rows;
  try {
    rows = executor_->Execute(sql);
  } catch (...) {
    if (tracer != nullptr) {
      executor_->SetExecutionTracer(nullptr);
      if (replacer != nullptr) {
        replacer->ClearEventObserver();
      }
    }
    throw;
  }
  const auto end = std::chrono::steady_clock::now();

  if (tracer != nullptr) {
    executor_->SetExecutionTracer(nullptr);
    if (replacer != nullptr) {
      replacer->ClearEventObserver();
      tracer->RecordCARSnapshot("FIN", replacer->GetSnapshot());
    }
  }

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
  result.output_columns = executor_->GetLastOutputColumns();
  result.plan_type = executor_->GetLastPlanType();
  result.metrics = metrics;
  if (tracer != nullptr) {
    result.trace = tracer->Finish();
  }
  if (!visualization_path_.empty()) {
    QueryVisualizer::WriteHtml(result, visualization_path_);
    result.visualization_path = visualization_path_;
  }
  return result;
}

void QueryProfiler::SetVisualizationPath(std::string path) {
  visualization_path_ = std::move(path);
  if (!visualization_path_.empty()) {
    tracing_enabled_ = true;
  }
}

const std::string &QueryProfiler::GetVisualizationPath() const {
  return visualization_path_;
}

void QueryProfiler::SetTracingEnabled(bool enabled) {
  tracing_enabled_ = enabled;
}

bool QueryProfiler::IsTracingEnabled() const {
  return tracing_enabled_;
}

}  // namespace minisgbd
