#include "storage/catalog_manager.h"

#include <stdexcept>

namespace minisgbd {

CatalogManager::CatalogManager(BufferPoolManager *buffer_pool)
    : buffer_pool_(buffer_pool) {
  if (buffer_pool_ == nullptr) {
    throw std::invalid_argument(
        "CatalogManager requiere un BufferPoolManager valido.");
  }
  EnsureCatalog();
}

void CatalogManager::EnsureCatalog() {
  if (buffer_pool_->GetPageCount() == 0) {
    page_id_t catalog_page_id = INVALID_PAGE_ID;
    Page *page = buffer_pool_->NewPage(&catalog_page_id);
    if (page == nullptr || catalog_page_id != CATALOG_PAGE_ID) {
      throw std::runtime_error(
          "No se pudo crear la pagina de catalogo.");
    }

    auto *catalog = reinterpret_cast<CatalogPage *>(page->get_data());
    catalog->Init();
    buffer_pool_->UnpinPage(catalog_page_id, true);
    return;
  }

  Page *page = buffer_pool_->FetchPage(CATALOG_PAGE_ID);
  if (page == nullptr) {
    throw std::runtime_error(
        "No se pudo leer la pagina de catalogo.");
  }

  auto *catalog = reinterpret_cast<CatalogPage *>(page->get_data());
  const bool is_valid = catalog->IsValid();
  const bool has_known_magic = catalog->HasKnownMagic();
  const uint32_t version = catalog->GetVersion();
  buffer_pool_->UnpinPage(CATALOG_PAGE_ID, false);

  if (!is_valid) {
    if (has_known_magic) {
      throw std::runtime_error(
          "La version " + std::to_string(version) +
          " del archivo es incompatible con el esquema personas "
          "(catalogo v" + std::to_string(CATALOG_VERSION) + ").");
    }
    throw std::runtime_error(
        "El archivo no contiene un catalogo Mini-SGBD valido.");
  }
}

CatalogPage CatalogManager::Read() {
  Page *page = buffer_pool_->FetchPage(CATALOG_PAGE_ID);
  if (page == nullptr) {
    throw std::runtime_error("No se pudo leer el catalogo.");
  }

  const auto *catalog =
      reinterpret_cast<const CatalogPage *>(page->get_data());
  if (!catalog->IsValid()) {
    buffer_pool_->UnpinPage(CATALOG_PAGE_ID, false);
    throw std::runtime_error("Catalogo Mini-SGBD invalido.");
  }

  CatalogPage copy = *catalog;
  buffer_pool_->UnpinPage(CATALOG_PAGE_ID, false);
  return copy;
}

void CatalogManager::UpdateTable(page_id_t first_page_id,
                                 page_id_t last_page_id,
                                 uint32_t tuple_count) {
  Page *page = buffer_pool_->FetchPage(CATALOG_PAGE_ID);
  if (page == nullptr) {
    throw std::runtime_error("No se pudo actualizar el catalogo.");
  }

  auto *catalog = reinterpret_cast<CatalogPage *>(page->get_data());
  catalog->SetTableFirstPageId(first_page_id);
  catalog->SetTableLastPageId(last_page_id);
  catalog->SetTableTupleCount(tuple_count);
  buffer_pool_->UnpinPage(CATALOG_PAGE_ID, true);
}

void CatalogManager::SetIndexDirectoryPageId(
    page_id_t directory_page_id) {
  Page *page = buffer_pool_->FetchPage(CATALOG_PAGE_ID);
  if (page == nullptr) {
    throw std::runtime_error(
        "No se pudo persistir la raiz del indice.");
  }

  auto *catalog = reinterpret_cast<CatalogPage *>(page->get_data());
  catalog->SetIndexDirectoryPageId(directory_page_id);
  buffer_pool_->UnpinPage(CATALOG_PAGE_ID, true);
}

}  // namespace minisgbd
