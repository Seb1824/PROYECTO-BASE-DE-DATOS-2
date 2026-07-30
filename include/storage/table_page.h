#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include "storage/disk_manager.h"
#include "storage/person_record.h"

namespace minisgbd {

struct TableRecord {
  int32_t id{0};
  std::array<char, PERSON_NAME_STORAGE_SIZE> nombre{};
  std::array<char, PERSON_CITY_STORAGE_SIZE> ciudad{};
  std::array<char, PERSON_PROFESSION_STORAGE_SIZE> profesion{};
  uint8_t occupied{0};
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

  bool Insert(const PersonRecord &person, uint32_t *slot = nullptr) {
    const TableRecord record = Encode(person);
    for (uint32_t index = 0; index < size_; ++index) {
      if (IsDeleted(index)) {
        records_[index] = record;
        if (slot != nullptr) {
          *slot = index;
        }
        return true;
      }
    }

    if (size_ >= TABLE_PAGE_CAPACITY) {
      return false;
    }
    records_[size_] = record;
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
    return slot >= size_ || records_[slot].occupied == 0;
  }

  PersonRecord GetRecord(uint32_t slot) const {
    if (IsDeleted(slot)) {
      throw std::out_of_range(
          "No existe un registro activo en el slot solicitado.");
    }
    return Decode(records_[slot]);
  }

  bool Update(uint32_t slot, const PersonRecord &person) {
    if (IsDeleted(slot)) {
      return false;
    }
    records_[slot] = Encode(person);
    return true;
  }

  bool Delete(uint32_t slot) {
    if (IsDeleted(slot)) {
      return false;
    }
    records_[slot] = TableRecord{};
    return true;
  }

  bool Restore(uint32_t slot, const PersonRecord &person) {
    if (slot >= size_ || !IsDeleted(slot)) {
      return false;
    }
    records_[slot] = Encode(person);
    return true;
  }

  page_id_t GetNextPageId() const { return next_page_id_; }
  void SetNextPageId(page_id_t page_id) { next_page_id_ = page_id; }

 private:
  template <std::size_t Size>
  static void EncodeText(const std::string &source,
                         std::array<char, Size> *destination,
                         const char *column) {
    if (source.size() >= Size) {
      throw std::invalid_argument(
          std::string("El campo ") + column + " supera " +
          std::to_string(Size - 1) + " bytes.");
    }
    if (source.find('\0') != std::string::npos) {
      throw std::invalid_argument(
          std::string("El campo ") + column +
          " no puede contener bytes nulos.");
    }
    destination->fill('\0');
    std::copy(source.begin(), source.end(), destination->begin());
  }

  template <std::size_t Size>
  static std::string DecodeText(const std::array<char, Size> &source) {
    const auto end =
        std::find(source.begin(), source.end(), '\0');
    return std::string(source.begin(), end);
  }

  static TableRecord Encode(const PersonRecord &person) {
    TableRecord record;
    record.id = person.id;
    EncodeText(person.nombre, &record.nombre, "nombre");
    EncodeText(person.ciudad, &record.ciudad, "ciudad");
    EncodeText(person.profesion, &record.profesion, "profesion");
    record.occupied = 1;
    return record;
  }

  static PersonRecord Decode(const TableRecord &record) {
    return PersonRecord{
        record.id,
        DecodeText(record.nombre),
        DecodeText(record.ciudad),
        DecodeText(record.profesion),
    };
  }

  page_id_t next_page_id_{INVALID_PAGE_ID};
  uint32_t size_{0};
  TableRecord records_[TABLE_PAGE_CAPACITY];
};

static_assert(sizeof(TablePage) <= PAGE_SIZE,
              "TablePage debe caber en una pagina fisica.");

}  // namespace minisgbd
