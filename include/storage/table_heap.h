#pragma once

#include <cstdint>

#include "buffer/buffer_pool_manager.h"
#include "storage/catalog_manager.h"

namespace minisgbd {

class TableHeap {
 public:
  explicit TableHeap(BufferPoolManager *buffer_pool);

  bool Insert(int key, int value);

  page_id_t GetFirstPageId() const;
  page_id_t GetLastPageId() const;
  uint32_t GetTupleCount() const;
  BufferPoolManager *GetBufferPool() const;

 private:
  BufferPoolManager *buffer_pool_;
  CatalogManager catalog_;
  page_id_t first_page_id_{INVALID_PAGE_ID};
  page_id_t last_page_id_{INVALID_PAGE_ID};
  uint32_t tuple_count_{0};
};

}  // namespace minisgbd
