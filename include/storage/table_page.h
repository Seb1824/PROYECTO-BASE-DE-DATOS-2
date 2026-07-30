#pragma once

#include <cstdint>
#include <limits>

#include "storage/disk_manager.h"

namespace minisgbd {

struct TableRecord {
  int key{0};
  int value{0};
};

constexpr int TABLE_TOMBSTONE_KEY = std::numeric_limits<int>::min();
constexpr int TABLE_PAGE_HEADER_SIZE =
    sizeof(page_id_t) + sizeof(uint32_t);
constexpr int TABLE_PAGE_CAPACITY =
    (PAGE_SIZE - TABLE_PAGE_HEADER_SIZE) / sizeof(TableRecord);

class TablePage {
 public:
  void Init() {
    next_page_id_ = INVALID_PAGE_ID;
    size_ = 0;
  }

  bool Insert(int key, int value, uint32_t *slot = nullptr) {
    for (uint32_t index = 0; index < size_; ++index) {
      if (IsDeleted(index)) {
        records_[index] = TableRecord{key, value};
        if (slot != nullptr) {
          *slot = index;
        }
        return true;
      }
    }

    if (size_ >= TABLE_PAGE_CAPACITY) {
      return false;
    }
    records_[size_] = TableRecord{key, value};
    if (slot != nullptr) {
      *slot = size_;
    }
    ++size_;
    return true;
  }

  bool IsFull() const {
    if (size_ < TABLE_PAGE_CAPACITY) {
      return false;
    }
    for (uint32_t index = 0; index < size_; ++index) {
      if (IsDeleted(index)) {
        return false;
      }
    }
    return true;
  }

  uint32_t GetSize() const { return size_; }
  bool IsDeleted(uint32_t slot) const {
    return slot >= size_ || records_[slot].key == TABLE_TOMBSTONE_KEY;
  }

  TableRecord GetRecord(uint32_t slot) const {
    return records_[slot];
  }

  bool Update(uint32_t slot, int key, int value) {
    if (IsDeleted(slot)) {
      return false;
    }
    records_[slot] = TableRecord{key, value};
    return true;
  }

  bool Delete(uint32_t slot) {
    if (IsDeleted(slot)) {
      return false;
    }
    records_[slot] = TableRecord{TABLE_TOMBSTONE_KEY, 0};
    return true;
  }

  bool Restore(uint32_t slot, int key, int value) {
    if (slot >= size_ || !IsDeleted(slot)) {
      return false;
    }
    records_[slot] = TableRecord{key, value};
    return true;
  }

  page_id_t GetNextPageId() const { return next_page_id_; }
  void SetNextPageId(page_id_t page_id) { next_page_id_ = page_id; }

 private:
  page_id_t next_page_id_{INVALID_PAGE_ID};
  uint32_t size_{0};
  TableRecord records_[TABLE_PAGE_CAPACITY];
};

static_assert(sizeof(TablePage) <= PAGE_SIZE,
              "TablePage debe caber en una pagina fisica.");

}  // namespace minisgbd
