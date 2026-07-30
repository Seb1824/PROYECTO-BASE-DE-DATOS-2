#pragma once

#include <string>
#include <vector>

#include "index/extensible_hash_table.h"
#include "query/query.h"
#include "query/tuple.h"

namespace minisgbd {

enum class QueryPlanType {
  kSeqScan,
  kFilteredSeqScan,
  kIndexScan,
};

class QueryExecutor {
 public:
  QueryExecutor(std::string table_name, const std::vector<Tuple> &tuples,
                ExtensibleHashTable *hash_index = nullptr);

  std::vector<Tuple> Execute(const std::string &sql);
  std::vector<Tuple> Execute(const SelectQuery &query);

  QueryPlanType GetLastPlanType() const;

 private:
  std::string table_name_;
  const std::vector<Tuple> &tuples_;
  ExtensibleHashTable *hash_index_;
  QueryPlanType last_plan_type_{QueryPlanType::kSeqScan};
};

}  // namespace minisgbd
