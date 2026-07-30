#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "storage/disk_manager.h"

namespace minisgbd {

using frame_id_t = int32_t;
constexpr frame_id_t INVALID_FRAME_ID = -1;

struct CARStateSnapshot {
  std::size_t capacity{0};
  std::size_t evictable_count{0};
  double target_p{0.0};
  std::size_t hits{0};
  std::size_t misses{0};
  std::vector<page_id_t> t1;
  std::vector<page_id_t> t2;
  std::vector<page_id_t> b1;
  std::vector<page_id_t> b2;
};

struct CAREvent {
  std::string type;
  page_id_t page_id{INVALID_PAGE_ID};
  frame_id_t frame_id{INVALID_FRAME_ID};
  double previous_p{0.0};
  CARStateSnapshot state;
};

class CARReplacer {
 public:
  using EventObserver = std::function<void(const CAREvent &)>;

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

  CARStateSnapshot GetSnapshot() const;
  void SetEventObserver(EventObserver observer);
  void ClearEventObserver();

 private:
  enum class ListTag { kNone, kT1, kT2 };

  struct FrameEntry {
    page_id_t page_id = INVALID_PAGE_ID;
    bool reference_bit = false;
    bool is_dirty = false;
    bool evictable = false;
    ListTag list = ListTag::kNone;
  };

  void PushGhost(std::list<page_id_t> *ghost_list, page_id_t page_id);

  void TrimGhostLists();

  CARStateSnapshot SnapshotLocked() const;
  void NotifyLocked(const std::string &type, page_id_t page_id,
                    frame_id_t frame_id, double previous_p);

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

  EventObserver observer_;
  mutable std::mutex latch_;
};

} 
