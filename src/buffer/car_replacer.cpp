#include "buffer/car_replacer.h"

#include <algorithm>
#include <utility>

namespace minisgbd {

CARReplacer::CARReplacer(size_t num_frames) : capacity_(num_frames) {}

bool CARReplacer::RecordAccess(page_id_t page_id, frame_id_t *frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  
  // 1. Si ya está en caché (Hit en T1 o T2)
  auto page_it = page_to_frame_.find(page_id);
  if (page_it != page_to_frame_.end()) {
    if (frame_id != nullptr) {
      *frame_id = page_it->second;
    }
    frames_[page_it->second].reference_bit = true;
    hits_++;
    NotifyLocked("HIT", page_id, page_it->second, p_);
    return true;
  }

  misses_++;
  const double previous_p = p_;

  // 2. Si hubo un fallo (Miss), buscamos en el historial B1
  auto it_b1 = std::find(b1_.begin(), b1_.end(), page_id);
  if (it_b1 != b1_.end()) {
    size_t b1_size = std::max<size_t>(1, b1_.size());
    size_t b2_size = std::max<size_t>(1, b2_.size());
    double step = (b1_size >= b2_size) ? 1.0 : static_cast<double>(b2_size) / b1_size;
    p_ = std::min(static_cast<double>(capacity_), p_ + step);
    b1_.erase(it_b1);
    NotifyLocked("MISS_B1", page_id, INVALID_FRAME_ID, previous_p);
    return false;
  }

  // 3. Fallo, pero está en el historial B2
  auto it_b2 = std::find(b2_.begin(), b2_.end(), page_id);
  if (it_b2 != b2_.end()) {
    size_t b1_size = std::max<size_t>(1, b1_.size());
    size_t b2_size = std::max<size_t>(1, b2_.size());
    double step = (b2_size >= b1_size) ? 1.0 : static_cast<double>(b1_size) / b2_size;
    p_ = std::max(0.0, p_ - step);
    b2_.erase(it_b2);
    NotifyLocked("MISS_B2", page_id, INVALID_FRAME_ID, previous_p);
    return false;
  }

  NotifyLocked("MISS_COLD", page_id, INVALID_FRAME_ID, previous_p);
  return false;
}

void CARReplacer::RecordInsertion(page_id_t page_id, frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  frames_[frame_id].page_id = page_id;
  frames_[frame_id].reference_bit = false;
  frames_[frame_id].is_dirty = false;
  frames_[frame_id].evictable = false;
  frames_[frame_id].list = ListTag::kT1;
  t1_.push_back(frame_id);
  page_to_frame_[page_id] = frame_id;
  NotifyLocked("INSERT_T1", page_id, frame_id, p_);
}

bool CARReplacer::Victim(frame_id_t *frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  if (frame_id == nullptr || evictable_count_ == 0) {
    return false;
  }

  auto has_evictable = [this](const std::list<frame_id_t> &frames) {
    return std::any_of(frames.begin(), frames.end(),
                       [this](frame_id_t candidate) {
                         auto it = frames_.find(candidate);
                         return it != frames_.end() && it->second.evictable;
                       });
  };

  while (evictable_count_ > 0) {
    const bool t1_has_evictable = has_evictable(t1_);
    const bool t2_has_evictable = has_evictable(t2_);

    if (!t1_has_evictable && !t2_has_evictable) {
      evictable_count_ = 0;
      return false;
    }

    const size_t target_t1_size =
        std::max<size_t>(1, static_cast<size_t>(p_));
    const bool prefer_t1 = t1_.size() >= target_t1_size;
    const bool use_t1 =
        (prefer_t1 && t1_has_evictable) || !t2_has_evictable;

    if (use_t1) {
      frame_id_t f = t1_.front();
      t1_.pop_front();
      FrameEntry &entry = frames_[f];

      if (!entry.reference_bit) {
        if (entry.evictable) {
          const page_id_t victim_page_id = entry.page_id;
          *frame_id = f;
          PushGhost(&b1_, entry.page_id);
          page_to_frame_.erase(entry.page_id);
          frames_.erase(f);
          --evictable_count_;
          NotifyLocked("EVICT_T1_B1", victim_page_id, f, p_);
          return true;
        }
        t1_.push_back(f);
      } else {
        const page_id_t promoted_page_id = entry.page_id;
        entry.reference_bit = false;
        entry.list = ListTag::kT2;
        t2_.push_back(f);
        NotifyLocked("PROMOTE_T1_T2", promoted_page_id, f, p_);
      }
    } else {
      frame_id_t f = t2_.front();
      t2_.pop_front();
      FrameEntry &entry = frames_[f];

      if (!entry.reference_bit) {
        if (entry.evictable) {
          const page_id_t victim_page_id = entry.page_id;
          *frame_id = f;
          PushGhost(&b2_, entry.page_id);
          page_to_frame_.erase(entry.page_id);
          frames_.erase(f);
          --evictable_count_;
          NotifyLocked("EVICT_T2_B2", victim_page_id, f, p_);
          return true;
        }
        t2_.push_back(f);
      } else {
        const page_id_t second_chance_page_id = entry.page_id;
        entry.reference_bit = false;
        t2_.push_back(f);
        NotifyLocked("SECOND_CHANCE_T2", second_chance_page_id, f, p_);
      }
    }
  }

  return false;
}

void CARReplacer::PushGhost(std::list<page_id_t> *ghost_list, page_id_t page_id) {
  ghost_list->push_back(page_id);
  TrimGhostLists();
}

void CARReplacer::TrimGhostLists() {
  while (t1_.size() + b1_.size() > capacity_) {
    if (!b1_.empty()) b1_.pop_front(); else break;
  }
  while (t1_.size() + t2_.size() + b1_.size() + b2_.size() > 2 * capacity_) {
    if (!b2_.empty()) b2_.pop_front(); else break;
  }
}

void CARReplacer::Pin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  auto it = frames_.find(frame_id);
  if (it != frames_.end() && it->second.evictable) {
    it->second.evictable = false;
    --evictable_count_;
  }
}

void CARReplacer::Unpin(frame_id_t frame_id, bool is_dirty) {
  std::lock_guard<std::mutex> lock(latch_);
  auto it = frames_.find(frame_id);
  if (it != frames_.end()) {
    if (!it->second.evictable) {
      it->second.evictable = true;
      ++evictable_count_;
    }
    if (is_dirty) {
      it->second.is_dirty = true;
    }
  }
}

size_t CARReplacer::Size() const {
  std::lock_guard<std::mutex> lock(latch_);
  return evictable_count_;
}

size_t CARReplacer::GetHits() const {
  std::lock_guard<std::mutex> lock(latch_);
  return hits_;
}

size_t CARReplacer::GetMisses() const {
  std::lock_guard<std::mutex> lock(latch_);
  return misses_;
}

double CARReplacer::GetHitRate() const {
  std::lock_guard<std::mutex> lock(latch_);
  return (hits_ + misses_ == 0)
             ? 0.0
             : static_cast<double>(hits_) / (hits_ + misses_);
}

CARStateSnapshot CARReplacer::GetSnapshot() const {
  std::lock_guard<std::mutex> lock(latch_);
  return SnapshotLocked();
}

void CARReplacer::SetEventObserver(EventObserver observer) {
  std::lock_guard<std::mutex> lock(latch_);
  observer_ = std::move(observer);
}

void CARReplacer::ClearEventObserver() {
  std::lock_guard<std::mutex> lock(latch_);
  observer_ = nullptr;
}

CARStateSnapshot CARReplacer::SnapshotLocked() const {
  CARStateSnapshot snapshot;
  snapshot.capacity = capacity_;
  snapshot.evictable_count = evictable_count_;
  snapshot.target_p = p_;
  snapshot.hits = hits_;
  snapshot.misses = misses_;

  auto append_resident_pages =
      [this](const std::list<frame_id_t> &source,
             std::vector<page_id_t> *destination) {
        destination->reserve(source.size());
        for (frame_id_t frame_id : source) {
          auto it = frames_.find(frame_id);
          if (it != frames_.end()) {
            destination->push_back(it->second.page_id);
          }
        }
      };

  append_resident_pages(t1_, &snapshot.t1);
  append_resident_pages(t2_, &snapshot.t2);
  snapshot.b1.assign(b1_.begin(), b1_.end());
  snapshot.b2.assign(b2_.begin(), b2_.end());
  return snapshot;
}

void CARReplacer::NotifyLocked(const std::string &type, page_id_t page_id,
                               frame_id_t frame_id, double previous_p) {
  if (!observer_) {
    return;
  }

  CAREvent event;
  event.type = type;
  event.page_id = page_id;
  event.frame_id = frame_id;
  event.previous_p = previous_p;
  event.state = SnapshotLocked();
  observer_(event);
}

}  // namespace minisgbd
