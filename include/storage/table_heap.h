#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "storage/catalog_manager.h"
#include "storage/rid.h"

namespace minisgbd {

struct LocatedRecord {
  RID rid;
  int key{0};
  int value{0};
};

class TableHeap {
 public:
  explicit TableHeap(BufferPoolManager *buffer_pool);

  bool Insert(int key, int value);
  std::optional<RID> InsertTuple(int key, int value);
  bool RollbackInsert(const RID &rid);
  bool GetRecord(const RID &rid, int *key, int *value);
  bool UpdateRecord(const RID &rid, int key, int value);
  bool DeleteRecord(const RID &rid);
  bool RestoreRecord(const RID &rid, int key, int value);
  std::vector<LocatedRecord> ReadAll();

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
