#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "query/execution_trace.h"
#include "query/query_executor.h"
#include "query/tuple.h"
#include "storage/disk_manager.h"

namespace minisgbd {

struct QueryMetrics {
  double elapsed_ms{0.0};
  uint64_t buffer_hits{0};
  uint64_t buffer_misses{0};
  double buffer_hit_ratio{0.0};
  uint64_t disk_reads{0};
  uint64_t disk_writes{0};
  uint64_t io_operations{0};
};

struct ProfiledQueryResult {
  std::vector<Tuple> rows;
  std::vector<std::string> output_columns{
      "id", "nombre", "ciudad", "profesion"};
  QueryPlanType plan_type{QueryPlanType::kSeqScan};
  QueryMetrics metrics;
  ExecutionTraceData trace;
  std::string visualization_path;
};

class QueryProfiler {
 public:
  QueryProfiler(QueryExecutor *executor, BufferPoolManager *buffer_pool = nullptr,
                DiskManager *disk_manager = nullptr);

  ProfiledQueryResult Execute(const std::string &sql);
  void SetVisualizationPath(std::string path);
  const std::string &GetVisualizationPath() const;
  void SetTracingEnabled(bool enabled);
  bool IsTracingEnabled() const;

 private:
  QueryExecutor *executor_;
  BufferPoolManager *buffer_pool_;
  DiskManager *disk_manager_;
  std::string visualization_path_;
  bool tracing_enabled_{false};
};

}  // namespace minisgbd
