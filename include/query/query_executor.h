#pragma once

#include <string>
#include <vector>

#include "index/extensible_hash_table.h"
#include "query/query.h"
#include "query/tuple.h"
#include "storage/table_heap.h"

namespace minisgbd {

enum class QueryPlanType {
  kSeqScan,
  kFilteredSeqScan,
  kIndexScan,
  kInsert,
};

class QueryExecutor {
 public:
  QueryExecutor(std::string table_name, const std::vector<Tuple> &tuples,
                ExtensibleHashTable *hash_index = nullptr);
  QueryExecutor(std::string table_name, TableHeap *table_heap,
                ExtensibleHashTable *hash_index = nullptr);

  std::vector<Tuple> Execute(const std::string &sql);
  std::vector<Tuple> Execute(const SelectQuery &query);
  std::vector<Tuple> Execute(const InsertQuery &query);

  QueryPlanType GetLastPlanType() const;

 private:
  std::string table_name_;
  const std::vector<Tuple> *tuples_{nullptr};
  TableHeap *table_heap_{nullptr};
  ExtensibleHashTable *hash_index_;
  QueryPlanType last_plan_type_{QueryPlanType::kSeqScan};
};

}  // namespace minisgbd
