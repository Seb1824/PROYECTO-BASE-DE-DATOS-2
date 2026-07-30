#pragma once

#include <cstdint>

#include "storage/disk_manager.h"

namespace minisgbd {

struct TableRecord {
  int key{0};
  int value{0};
};

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

  bool Insert(int key, int value) {
    if (IsFull()) {
      return false;
    }
    records_[size_] = TableRecord{key, value};
    ++size_;
    return true;
  }

  bool IsFull() const { return size_ >= TABLE_PAGE_CAPACITY; }
  uint32_t GetSize() const { return size_; }

  TableRecord GetRecord(uint32_t slot) const {
    return records_[slot];
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
