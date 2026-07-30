#pragma once

#include <cstddef>
#include <vector>

#include "index/extensible_hash_table.h"
#include "query/operator.h"
#include "storage/table_heap.h"

namespace minisgbd {

class IndexScanOperator : public Operator {
 public:
  IndexScanOperator(ExtensibleHashTable *hash_index, TableHeap *table_heap,
                    int search_key);
  ~IndexScanOperator() override = default;

  void Open() override;
  bool Next(Tuple *tuple) override;
  void Close() override;

 private:
  ExtensibleHashTable *hash_index_;
  TableHeap *table_heap_;
  int search_key_;

  std::vector<Tuple> results_;
  std::size_t cursor_{0};
  bool initialized_{false};
};

}  // namespace minisgbd
