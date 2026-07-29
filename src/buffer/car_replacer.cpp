#include "buffer/car_replacer.h"
#include <algorithm>

namespace minisgbd {

CARReplacer::CARReplacer(size_t num_frames) : capacity_(num_frames) {}

bool CARReplacer::RecordAccess(page_id_t page_id, frame_id_t *frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  
  // 1. Si ya está en caché (Hit en T1 o T2)
  if (page_to_frame_.find(page_id) != page_to_frame_.end()) {
    *frame_id = page_to_frame_[page_id];
    frames_[*frame_id].reference_bit = true;
    hits_++;
    return true;
  }

  misses_++;

  // 2. Si hubo un fallo (Miss), buscamos en el historial B1
  auto it_b1 = std::find(b1_.begin(), b1_.end(), page_id);
  if (it_b1 != b1_.end()) {
    size_t b1_size = std::max<size_t>(1, b1_.size());
    size_t b2_size = std::max<size_t>(1, b2_.size());
    double step = (b1_size >= b2_size) ? 1.0 : static_cast<double>(b2_size) / b1_size;
    p_ = std::min(static_cast<double>(capacity_), p_ + step);
    b1_.erase(it_b1);
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
    return false;
  }

  return false;
}

void CARReplacer::RecordInsertion(page_id_t page_id, frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  frames_[frame_id].page_id = page_id;
  frames_[frame_id].reference_bit = false;
  frames_[frame_id].is_dirty = false;
  frames_[frame_id].evictable = true;
  frames_[frame_id].list = ListTag::kT1;
  t1_.push_back(frame_id);
  page_to_frame_[page_id] = frame_id;
}

bool CARReplacer::Victim(frame_id_t *frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  if (t1_.empty() && t2_.empty()) return false;

  while (true) {
    if (t1_.size() >= std::max<size_t>(1, static_cast<size_t>(p_))) {
      frame_id_t f = t1_.front();
      t1_.pop_front();
      if (!frames_[f].reference_bit) {
        if (frames_[f].evictable) {
          *frame_id = f;
          PushGhost(&b1_, frames_[f].page_id);
          page_to_frame_.erase(frames_[f].page_id);
          frames_.erase(f);
          return true;
        } else {
          t1_.push_back(f);
        }
      } else {
        frames_[f].reference_bit = false;
        frames_[f].list = ListTag::kT2;
        t2_.push_back(f);
      }
    } else {
      if (t2_.empty()) continue; 
      frame_id_t f = t2_.front();
      t2_.pop_front();
      if (!frames_[f].reference_bit) {
        if (frames_[f].evictable) {
          *frame_id = f;
          PushGhost(&b2_, frames_[f].page_id);
          page_to_frame_.erase(frames_[f].page_id);
          frames_.erase(f);
          return true;
        } else {
          t2_.push_back(f);
        }
      } else {
        frames_[f].reference_bit = false;
        t2_.push_back(f);
      }
    }
  }
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
  if (frames_.count(frame_id)) frames_[frame_id].evictable = false;
}

void CARReplacer::Unpin(frame_id_t frame_id, bool is_dirty) {
  std::lock_guard<std::mutex> lock(latch_);
  if (frames_.count(frame_id)) {
    frames_[frame_id].evictable = true;
    if (is_dirty) frames_[frame_id].is_dirty = true;
  }
}

size_t CARReplacer::Size() const { return t1_.size() + t2_.size(); }
size_t CARReplacer::GetHits() const { return hits_; }
size_t CARReplacer::GetMisses() const { return misses_; }
double CARReplacer::GetHitRate() const { 
  return (hits_ + misses_ == 0) ? 0.0 : static_cast<double>(hits_) / (hits_ + misses_); 
}

frame_id_t CARReplacer::Replace() { return INVALID_FRAME_ID; }

} 