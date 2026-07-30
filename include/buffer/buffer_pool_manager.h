#pragma once

#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>

#include "buffer/car_replacer.h"
#include "buffer/page.h"
#include "storage/disk_manager.h"

namespace minisgbd {

class BufferPoolManager {
 public:
  BufferPoolManager(size_t pool_size, DiskManager *disk_manager,
                    CARReplacer *replacer);

  ~BufferPoolManager();

  BufferPoolManager(const BufferPoolManager &) = delete;
  BufferPoolManager &operator=(const BufferPoolManager &) = delete;

  Page *FetchPage(page_id_t page_id);

  bool UnpinPage(page_id_t page_id, bool is_dirty);

  bool FlushPage(page_id_t page_id);

  void FlushAllPages();

  Page *NewPage(page_id_t *page_id);

  size_t GetPoolSize() const;
  int GetPageCount() const;

  uint64_t GetHitCount() const;
  uint64_t GetMissCount() const;
  double GetHitRatio() const;

 private:
  bool TryGetFrame(frame_id_t *out_frame_id);

  size_t pool_size_;
  Page *pages_;

  DiskManager *disk_manager_;
  CARReplacer *replacer_;

  std::unordered_map<page_id_t, frame_id_t> page_table_;
  std::list<frame_id_t> free_list_;

  uint64_t hit_count_;
  uint64_t miss_count_;

  mutable std::mutex latch_;
};

} 
