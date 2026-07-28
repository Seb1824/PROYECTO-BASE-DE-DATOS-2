#include "buffer/page.h"

#include <cstring>

namespace minisgbd {

Page::Page() { reset_memory(); }

void Page::reset_memory() { std::memset(data_, 0, PAGE_SIZE); }

void Page::reset() {
  reset_memory();
  page_id_ = INVALID_PAGE_ID;
  pin_count_ = 0;
  is_dirty_ = false;
}

}  // namespace minisgbd
