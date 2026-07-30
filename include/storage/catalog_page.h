#pragma once

#include <cstdint>

#include "storage/disk_manager.h"

namespace minisgbd {

constexpr page_id_t CATALOG_PAGE_ID = 0;
constexpr uint32_t CATALOG_MAGIC = 0x4D534744;
constexpr uint32_t CATALOG_VERSION = 2;
constexpr uint32_t LEGACY_CATALOG_VERSION = 1;

class CatalogPage {
 public:
  void Init() {
    magic_ = CATALOG_MAGIC;
    version_ = CATALOG_VERSION;
    table_first_page_id_ = INVALID_PAGE_ID;
    table_last_page_id_ = INVALID_PAGE_ID;
    index_directory_page_id_ = INVALID_PAGE_ID;
    table_tuple_count_ = 0;
  }

  bool IsValid() const {
    return magic_ == CATALOG_MAGIC && version_ == CATALOG_VERSION;
  }

  bool IsLegacyVersion() const {
    return magic_ == CATALOG_MAGIC &&
           version_ == LEGACY_CATALOG_VERSION;
  }

  void MigrateLegacyIndexToRid() {
    version_ = CATALOG_VERSION;
    index_directory_page_id_ = INVALID_PAGE_ID;
  }

  page_id_t GetTableFirstPageId() const { return table_first_page_id_; }
  page_id_t GetTableLastPageId() const { return table_last_page_id_; }
  page_id_t GetIndexDirectoryPageId() const {
    return index_directory_page_id_;
  }
  uint32_t GetTableTupleCount() const { return table_tuple_count_; }

  void SetTableFirstPageId(page_id_t page_id) {
    table_first_page_id_ = page_id;
  }
  void SetTableLastPageId(page_id_t page_id) {
    table_last_page_id_ = page_id;
  }
  void SetIndexDirectoryPageId(page_id_t page_id) {
    index_directory_page_id_ = page_id;
  }
  void SetTableTupleCount(uint32_t count) { table_tuple_count_ = count; }

 private:
  uint32_t magic_{0};
  uint32_t version_{0};
  page_id_t table_first_page_id_{INVALID_PAGE_ID};
  page_id_t table_last_page_id_{INVALID_PAGE_ID};
  page_id_t index_directory_page_id_{INVALID_PAGE_ID};
  uint32_t table_tuple_count_{0};
};

static_assert(sizeof(CatalogPage) <= PAGE_SIZE,
              "CatalogPage debe caber en una pagina fisica.");

}  // namespace minisgbd
