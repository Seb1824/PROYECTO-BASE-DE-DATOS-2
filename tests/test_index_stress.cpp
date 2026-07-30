#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "index/hash_bucket_page.h"
#include "index/hash_directory_page.h"
#include "storage/catalog_manager.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"

using minisgbd::BufferPoolManager;
using minisgbd::CARReplacer;
using minisgbd::CatalogManager;
using minisgbd::BUCKET_ARRAY_SIZE;
using minisgbd::DIRECTORY_ARRAY_SIZE;
using minisgbd::DiskManager;
using minisgbd::ExtensibleHashTable;
using minisgbd::HashDirectoryPage;
using minisgbd::INVALID_PAGE_ID;
using minisgbd::Page;
using minisgbd::TableHeap;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

template <typename Exception, typename Function>
void ExpectException(Function function, const std::string &message) {
  try {
    function();
    Expect(false, message);
  } catch (const Exception &) {
    // Resultado esperado.
  } catch (const std::exception &error) {
    Expect(false, message + " Error inesperado: " + error.what());
  }
}

void TestThousandsOfKeysAndRestart() {
  const std::string db_file = "test_index_thousands.db";
  constexpr int key_count = 5000;
  std::remove(db_file.c_str());

  minisgbd::page_id_t directory_page_id = INVALID_PAGE_ID;

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(16);
    BufferPoolManager bpm(16, &disk_manager, &replacer);
    ExtensibleHashTable hash_index(&bpm);
    directory_page_id = hash_index.GetDirectoryPageId();

    for (int key = 0; key < key_count; ++key) {
      Expect(hash_index.Insert(key, key * 10),
             "Debe insertar miles de claves sin perder registros.");
    }

    Page *directory_page = bpm.FetchPage(directory_page_id);
    Expect(directory_page != nullptr,
           "Debe poder inspeccionar el directorio despues de los splits.");
    if (directory_page != nullptr) {
      const auto *directory = reinterpret_cast<const HashDirectoryPage *>(
          directory_page->get_data());
      Expect(directory->GetGlobalDepth() >= 4,
             "Cinco mil claves deben producir varios splits consecutivos.");
      bpm.UnpinPage(directory_page_id, false);
    }

    for (int key = 0; key < key_count; ++key) {
      int value = 0;
      Expect(hash_index.GetValue(key, &value) && value == key * 10,
             "Cada clave insertada debe conservar su valor.");
    }

    int value = 0;
    Expect(!hash_index.GetValue(-1, &value),
           "Una clave negativa inexistente debe devolver false.");
    Expect(!hash_index.GetValue(key_count + 100, &value),
           "Una clave superior al rango debe devolver false.");
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(16);
    BufferPoolManager bpm(16, &disk_manager, &replacer);
    ExtensibleHashTable hash_index(&bpm);

    Expect(hash_index.GetDirectoryPageId() == directory_page_id,
           "El reinicio debe recuperar la misma raiz del indice.");
    Expect(!hash_index.IsNewlyCreated(),
           "El indice recuperado no debe marcarse como nuevo.");

    for (int key = 0; key < key_count; ++key) {
      int value = 0;
      Expect(hash_index.GetValue(key, &value) && value == key * 10,
             "Las miles de claves deben sobrevivir al reinicio.");
    }

    Expect(!hash_index.Insert(2500, 999999),
           "Un duplicado debe rechazarse despues de reiniciar.");
    int value = 0;
    Expect(hash_index.GetValue(2500, &value) && value == 25000,
           "El duplicado rechazado no debe sobrescribir el valor original.");
    Expect(!hash_index.GetValue(999999, &value),
           "Una clave inexistente debe seguir ausente tras reiniciar.");
  }

  std::remove(db_file.c_str());
}

void TestDirectoryMaximumLimit() {
  const std::string db_file = "test_index_directory_limit.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    ExtensibleHashTable hash_index(&bpm);

    int maximum_depth = 0;
    while ((1U << (maximum_depth + 1)) <=
           static_cast<unsigned int>(DIRECTORY_ARRAY_SIZE)) {
      ++maximum_depth;
    }
    const int shared_low_bits = 1 << maximum_depth;

    for (int index = 0; index < BUCKET_ARRAY_SIZE; ++index) {
      const int key = index * shared_low_bits;
      Expect(hash_index.Insert(key, index),
             "Debe llenar el bucket antes de probar el limite.");
    }

    ExpectException<std::overflow_error>(
        [&hash_index, shared_low_bits]() {
          hash_index.Insert(BUCKET_ARRAY_SIZE * shared_low_bits,
                            BUCKET_ARRAY_SIZE);
        },
        "Debe rechazar el crecimiento que excede una pagina de directorio.");

    Page *directory_page =
        bpm.FetchPage(hash_index.GetDirectoryPageId());
    Expect(directory_page != nullptr,
           "El directorio debe seguir accesible despues del limite.");
    if (directory_page != nullptr) {
      const auto *directory = reinterpret_cast<const HashDirectoryPage *>(
          directory_page->get_data());
      Expect((1U << directory->GetGlobalDepth()) <=
                 static_cast<unsigned int>(DIRECTORY_ARRAY_SIZE),
             "La profundidad global nunca debe desbordar el arreglo.");
      Expect(directory->GetGlobalDepth() == maximum_depth,
             "El directorio debe detenerse en su profundidad maxima.");
      bpm.UnpinPage(hash_index.GetDirectoryPageId(), false);
    }

    int value = 0;
    Expect(hash_index.GetValue(100 * shared_low_bits, &value) &&
               value == 100,
           "El indice debe conservar los datos tras alcanzar el limite.");
  }

  std::remove(db_file.c_str());
}

void TestInvalidCatalog() {
  const std::string db_file = "test_invalid_catalog.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    Expect(disk_manager.allocate_page() == 0,
           "El archivo invalido debe contener una pagina cero.");
  }

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(4);
    BufferPoolManager bpm(4, &disk_manager, &replacer);
    ExpectException<std::runtime_error>(
        [&bpm]() {
          TableHeap table_heap(&bpm);
        },
        "Debe rechazar un archivo sin la firma valida del catalogo.");
  }

  std::remove(db_file.c_str());
}

void TestIncompleteCatalog() {
  const std::string invalid_index_file = "test_incomplete_index_catalog.db";
  std::remove(invalid_index_file.c_str());

  {
    DiskManager disk_manager(invalid_index_file);
    CARReplacer replacer(4);
    BufferPoolManager bpm(4, &disk_manager, &replacer);
    CatalogManager catalog(&bpm);
    catalog.SetIndexDirectoryPageId(999);
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(invalid_index_file);
    CARReplacer replacer(4);
    BufferPoolManager bpm(4, &disk_manager, &replacer);
    ExpectException<std::runtime_error>(
        [&bpm]() {
          ExtensibleHashTable hash_index(&bpm);
        },
        "Debe rechazar una raiz de indice fuera del archivo.");
  }
  std::remove(invalid_index_file.c_str());

  const std::string invalid_table_file = "test_incomplete_table_catalog.db";
  std::remove(invalid_table_file.c_str());

  {
    DiskManager disk_manager(invalid_table_file);
    CARReplacer replacer(4);
    BufferPoolManager bpm(4, &disk_manager, &replacer);
    CatalogManager catalog(&bpm);
    catalog.UpdateTable(1, INVALID_PAGE_ID, 1);
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(invalid_table_file);
    CARReplacer replacer(4);
    BufferPoolManager bpm(4, &disk_manager, &replacer);
    ExpectException<std::runtime_error>(
        [&bpm]() {
          TableHeap table_heap(&bpm);
        },
        "Debe rechazar una cadena de tabla incompleta en el catalogo.");
  }

  std::remove(invalid_table_file.c_str());
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE ESTRES Y RECUPERACION DEL INDICE ===\n";

  TestThousandsOfKeysAndRestart();
  TestDirectoryMaximumLimit();
  TestInvalidCatalog();
  TestIncompleteCatalog();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas extendidas del indice pasaron.\n";
  return 0;
}
