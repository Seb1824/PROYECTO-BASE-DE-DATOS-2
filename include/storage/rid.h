#pragma once

#include <cstdint>
#include <stdexcept>

#include "storage/disk_manager.h"

namespace minisgbd {

constexpr uint32_t RID_SLOT_BITS = 10;
constexpr uint32_t RID_SLOT_MASK = (1U << RID_SLOT_BITS) - 1U;
constexpr page_id_t RID_MAX_PAGE_ID =
    static_cast<page_id_t>((1U << (31U - RID_SLOT_BITS)) - 1U);

struct RID {
  page_id_t page_id{INVALID_PAGE_ID};
  uint32_t slot{0};

  bool IsValid() const {
    return page_id >= 0 && page_id <= RID_MAX_PAGE_ID &&
           slot <= RID_SLOT_MASK;
  }

  int Encode() const {
    if (!IsValid()) {
      throw std::invalid_argument("No se puede codificar un RID invalido.");
    }
    return static_cast<int>(
        (static_cast<uint32_t>(page_id) << RID_SLOT_BITS) | slot);
  }

  static RID Decode(int encoded) {
    if (encoded < 0) {
      return RID{};
    }
    const uint32_t value = static_cast<uint32_t>(encoded);
    return RID{static_cast<page_id_t>(value >> RID_SLOT_BITS),
               value & RID_SLOT_MASK};
  }

  bool operator==(const RID &other) const {
    return page_id == other.page_id && slot == other.slot;
  }
};

}  // namespace minisgbd
