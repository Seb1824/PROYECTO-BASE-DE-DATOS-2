#pragma once

#include <mutex>

#include "storage/disk_manager.h"

namespace minisgbd {

class Page {
  friend class BufferPoolManager;

 public:
  Page();

  ~Page() = default;

  Page(const Page &) = delete;
  Page &operator=(const Page &) = delete;

  inline char *get_data() { return data_; }

  inline const char *get_data() const { return data_; }

  inline page_id_t get_page_id() const { return page_id_; }

  inline void set_page_id(page_id_t page_id) { page_id_ = page_id; }

  inline int get_pin_count() const { return pin_count_; }

  inline void increment_pin_count() { ++pin_count_; }

  inline void decrement_pin_count() {
    if (pin_count_ > 0) {
      --pin_count_;
    }
  }

  inline bool is_dirty() const { return is_dirty_; }

  inline void set_dirty(bool is_dirty) { is_dirty_ = is_dirty; }

  inline std::mutex &get_latch() { return latch_; }

  void reset_memory();

  void reset();

 private:
  char data_[PAGE_SIZE];

  page_id_t page_id_{INVALID_PAGE_ID};

  int pin_count_{0};

  bool is_dirty_{false};

  std::mutex latch_;
};

} 
