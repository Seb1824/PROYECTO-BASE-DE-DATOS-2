#pragma once
#include "buffer/buffer_pool_manager.h"
#include "index/hash_bucket_page.h"
#include "index/hash_directory_page.h"

namespace minisgbd {

class ExtensibleHashTable {
 public:
  explicit ExtensibleHashTable(BufferPoolManager *bpm);
  ~ExtensibleHashTable() = default;

  bool GetValue(KeyType key, ValueType *result);
  bool Insert(KeyType key, ValueType value);

  page_id_t GetDirectoryPageId() const;
  bool IsNewlyCreated() const;

 private:
  uint32_t Hash(KeyType key) const;
  uint32_t GetBucketIndex(KeyType key) const;
  
  BufferPoolManager *bpm_;
  page_id_t directory_page_id_{INVALID_PAGE_ID};
  bool newly_created_{false};
};

}  // namespace minisgbd
