#pragma once

#include <cstddef>
#include <vector>

#include "buffer/page.h"
#include "query/operator.h"
#include "storage/table_heap.h"

namespace minisgbd {

class SeqScanOperator : public Operator {
 public:
  explicit SeqScanOperator(const std::vector<Tuple> &tuples);
  explicit SeqScanOperator(TableHeap *table_heap);
  ~SeqScanOperator() override;

 protected:
  void DoOpen() override;
  bool DoNext(Tuple *tuple) override;
  void DoClose() override;

 private:
  void LoadPhysicalPage(page_id_t page_id);

  const std::vector<Tuple> *tuples_{nullptr};
  TableHeap *table_heap_{nullptr};
  Page *current_page_{nullptr};
  page_id_t current_page_id_{INVALID_PAGE_ID};
  std::size_t cursor_{0};
  bool initialized_{false};
};

}  // namespace minisgbd
