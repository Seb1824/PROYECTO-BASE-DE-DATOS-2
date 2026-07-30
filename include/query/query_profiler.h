#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
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
  QueryPlanType plan_type{QueryPlanType::kSeqScan};
  QueryMetrics metrics;
};

class QueryProfiler {
 public:
  QueryProfiler(QueryExecutor *executor, BufferPoolManager *buffer_pool = nullptr,
                DiskManager *disk_manager = nullptr);

  ProfiledQueryResult Execute(const std::string &sql);

 private:
  QueryExecutor *executor_;
  BufferPoolManager *buffer_pool_;
  DiskManager *disk_manager_;
};

}  // namespace minisgbd
