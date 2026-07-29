#pragma once
#include "buffer/page.h"

namespace minisgbd {

const int DIRECTORY_HEADER_SIZE = 4;
const int DIRECTORY_ARRAY_SIZE = (PAGE_SIZE - DIRECTORY_HEADER_SIZE) / sizeof(page_id_t);

class HashDirectoryPage {
 public:
  void Init(int global_depth) {
    global_depth_ = global_depth;
    for (int i = 0; i < DIRECTORY_ARRAY_SIZE; i++) {
      bucket_page_ids_[i] = INVALID_PAGE_ID;
    }
  }

  int GetGlobalDepth() const { return global_depth_; }
  void SetGlobalDepth(int global_depth) { global_depth_ = global_depth; }

  page_id_t GetBucketPageId(uint32_t bucket_idx) const {
    return bucket_page_ids_[bucket_idx];
  }

  void SetBucketPageId(uint32_t bucket_idx, page_id_t bucket_page_id) {
    bucket_page_ids_[bucket_idx] = bucket_page_id;
  }

 private:
  int global_depth_;
  page_id_t bucket_page_ids_[DIRECTORY_ARRAY_SIZE];
};

} // namespace minisgbd