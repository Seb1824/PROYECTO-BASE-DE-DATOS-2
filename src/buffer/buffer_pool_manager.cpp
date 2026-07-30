#include "buffer/buffer_pool_manager.h"

#include <stdexcept>

namespace minisgbd {

BufferPoolManager::BufferPoolManager(size_t pool_size,
                                     DiskManager *disk_manager,
                                     CARReplacer *replacer)
    : pool_size_(pool_size),
      pages_(new Page[pool_size]),
      disk_manager_(disk_manager),
      replacer_(replacer),
      hit_count_(0),
      miss_count_(0) {
  if (pool_size_ == 0) {
    delete[] pages_;
    throw std::invalid_argument(
        "BufferPoolManager requiere al menos un frame.");
  }
  if (disk_manager_ == nullptr || replacer_ == nullptr) {
    delete[] pages_;
    throw std::invalid_argument(
        "BufferPoolManager requiere DiskManager y CARReplacer validos.");
  }

  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.push_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() {
  try {
    FlushAllPages();
  } catch (...) {
    // Los destructores no deben propagar excepciones.
  }
  delete[] pages_;
}

bool BufferPoolManager::TryGetFrame(frame_id_t *out_frame_id) {
  if (!free_list_.empty()) {
    *out_frame_id = free_list_.front();
    free_list_.pop_front();
    return true;
  }

  if (!replacer_->Victim(out_frame_id)) {
    return false;
  }

  Page &victim_page = pages_[*out_frame_id];
  if (victim_page.is_dirty()) {
    disk_manager_->write_page(victim_page.get_page_id(),
                              victim_page.get_data());
  }

  page_table_.erase(victim_page.get_page_id());
  return true;
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    frame_id_t frame_id = it->second;
    Page &page = pages_[frame_id];

    if (page.get_pin_count() == 0) {
      replacer_->Pin(frame_id);
    }

    page.increment_pin_count();

    frame_id_t unused;
    replacer_->RecordAccess(page_id, &unused);
    ++hit_count_;

    return &page;
  }

  ++miss_count_;

  {
    frame_id_t unused;
    replacer_->RecordAccess(page_id, &unused);
  }

  frame_id_t frame_id;
  if (!TryGetFrame(&frame_id)) {
    return nullptr;
  }

  Page &page = pages_[frame_id];
  page.reset();
  disk_manager_->read_page(page_id, page.get_data());
  page.set_page_id(page_id);
  page.set_dirty(false);
  page.increment_pin_count();

  page_table_[page_id] = frame_id;
  replacer_->RecordInsertion(page_id, frame_id);

  return &page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  if (page.get_pin_count() <= 0) {
    return false;
  }

  if (is_dirty) {
    page.set_dirty(true);
  }

  page.decrement_pin_count();

  if (page.get_pin_count() == 0) {
    replacer_->Unpin(frame_id, page.is_dirty());
  }

  return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  if (page.is_dirty()) {
    disk_manager_->write_page(page_id, page.get_data());
    page.set_dirty(false);
  }

  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard<std::mutex> lock(latch_);

  for (auto &kv : page_table_) {
    frame_id_t frame_id = kv.second;
    Page &page = pages_[frame_id];

    if (page.is_dirty()) {
      disk_manager_->write_page(page.get_page_id(), page.get_data());
      page.set_dirty(false);
    }
  }
}

Page *BufferPoolManager::NewPage(page_id_t *page_id) {
  std::lock_guard<std::mutex> lock(latch_);

  frame_id_t frame_id;
  if (!TryGetFrame(&frame_id)) {
    if (page_id != nullptr) {
      *page_id = INVALID_PAGE_ID;
    }
    return nullptr;
  }

  page_id_t new_page_id = disk_manager_->allocate_page();

  Page &page = pages_[frame_id];
  page.reset();
  page.set_page_id(new_page_id);
  page.set_dirty(false);
  page.increment_pin_count();

  page_table_[new_page_id] = frame_id;
  replacer_->RecordInsertion(new_page_id, frame_id);

  if (page_id != nullptr) {
    *page_id = new_page_id;
  }

  return &page;
}

size_t BufferPoolManager::GetPoolSize() const {
  std::lock_guard<std::mutex> lock(latch_);
  return pool_size_;
}

uint64_t BufferPoolManager::GetHitCount() const {
  std::lock_guard<std::mutex> lock(latch_);
  return hit_count_;
}

uint64_t BufferPoolManager::GetMissCount() const {
  std::lock_guard<std::mutex> lock(latch_);
  return miss_count_;
}

double BufferPoolManager::GetHitRatio() const {
  std::lock_guard<std::mutex> lock(latch_);

  uint64_t total = hit_count_ + miss_count_;
  if (total == 0) {
    return 0.0;
  }

  return static_cast<double>(hit_count_) /
         static_cast<double>(total);
}

}
