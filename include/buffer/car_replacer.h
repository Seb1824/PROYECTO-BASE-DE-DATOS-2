#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>

#include "storage/disk_manager.h"

namespace minisgbd {

using frame_id_t = int32_t;
constexpr frame_id_t INVALID_FRAME_ID = -1;

class CARReplacer {
 public:
  explicit CARReplacer(size_t num_frames);
  ~CARReplacer() = default;

  CARReplacer(const CARReplacer &) = delete;
  CARReplacer &operator=(const CARReplacer &) = delete;

  bool RecordAccess(page_id_t page_id, frame_id_t *frame_id);

  bool Victim(frame_id_t *frame_id);

  void RecordInsertion(page_id_t page_id, frame_id_t frame_id);

  void Pin(frame_id_t frame_id);

  void Unpin(frame_id_t frame_id, bool is_dirty);

  size_t Size() const;

  size_t GetHits() const;
  size_t GetMisses() const;
  double GetHitRate() const;

 private:
  enum class ListTag { kNone, kT1, kT2 };

  struct FrameEntry {
    page_id_t page_id = INVALID_PAGE_ID;
    bool reference_bit = false;
    bool is_dirty = false;
    bool evictable = true;
    ListTag list = ListTag::kNone;
  };

  frame_id_t Replace();

  void PushGhost(std::list<page_id_t> *ghost_list, page_id_t page_id);

  void TrimGhostLists();

  const size_t capacity_;

  std::list<frame_id_t> t1_;
  std::list<frame_id_t> t2_;

  std::list<page_id_t> b1_;
  std::list<page_id_t> b2_;

  std::unordered_map<frame_id_t, FrameEntry> frames_;
  std::unordered_map<page_id_t, frame_id_t> page_to_frame_;

  double p_ = 0.0;

  size_t evictable_count_ = 0;

  size_t hits_ = 0;
  size_t misses_ = 0;

  mutable std::mutex latch_;
};

} 
