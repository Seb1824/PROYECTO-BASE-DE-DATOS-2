#pragma once

#include <cstdint>

#include "buffer/buffer_pool_manager.h"
#include "storage/catalog_page.h"

namespace minisgbd {

class CatalogManager {
 public:
  explicit CatalogManager(BufferPoolManager *buffer_pool);

  CatalogPage Read();
  void UpdateTable(page_id_t first_page_id, page_id_t last_page_id,
                   uint32_t tuple_count);
  void SetIndexDirectoryPageId(page_id_t directory_page_id);

 private:
  void EnsureCatalog();

  BufferPoolManager *buffer_pool_;
};

}  // namespace minisgbd
