#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "query/query_executor.h"
#include "query/query_profiler.h"
#include "query/tuple.h"
#include "storage/disk_manager.h"
#include "storage/catalog_page.h"
#include "storage/table_heap.h"
#include "storage/table_page.h"

using namespace minisgbd;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

void TestPhysicalTableAndPersistentIndex() {
  const std::string db_file = "test_persistence.db";
  constexpr int kTupleCount = TABLE_PAGE_CAPACITY + 89;
  std::remove(db_file.c_str());

  page_id_t original_directory_page_id = INVALID_PAGE_ID;
  int page_count_after_creation = 0;

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);

    Expect(hash_index.IsNewlyCreated(),
           "El indice debe crearse en una base nueva.");

    for (int key = 1; key <= kTupleCount; ++key) {
      const std::optional<RID> rid =
          table_heap.InsertTuple(key, key * 10);
      Expect(rid.has_value(),
             "Debe insertar cada registro en paginas fisicas.");
      Expect(rid.has_value() &&
                 hash_index.Insert(key, rid->Encode()),
             "Debe insertar cada registro en el indice.");
    }

    Expect(table_heap.GetTupleCount() == kTupleCount,
           "El catalogo debe contar todos los registros.");
    Expect(table_heap.GetFirstPageId() != table_heap.GetLastPageId(),
           "La carga debe ocupar mas de una pagina de tabla.");

    original_directory_page_id = hash_index.GetDirectoryPageId();
    bpm.FlushAllPages();
    page_count_after_creation = disk_manager.get_num_pages();
  }

  {
    DiskManager disk_manager(db_file);
    const int pages_before_open = disk_manager.get_num_pages();
    CARReplacer replacer(4);
    BufferPoolManager bpm(4, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);

    Expect(!hash_index.IsNewlyCreated(),
           "El indice debe recuperarse al reabrir el archivo.");
    Expect(hash_index.GetDirectoryPageId() ==
               original_directory_page_id,
           "La raiz recuperada debe coincidir con la original.");
    Expect(table_heap.GetTupleCount() == kTupleCount,
           "La tabla debe recuperar su cantidad de registros.");
    Expect(bpm.GetPageCount() == pages_before_open &&
               pages_before_open == page_count_after_creation,
           "Abrir tabla e indice no debe asignar paginas nuevas.");

    QueryExecutor executor("registros", &table_heap, &hash_index);
    QueryProfiler profiler(&executor, &bpm, &disk_manager);

    const ProfiledQueryResult all_rows =
        profiler.Execute("SELECT * FROM registros;");
    Expect(all_rows.rows.size() == kTupleCount,
           "SeqScan fisico debe recorrer todos los registros.");
    Expect(all_rows.plan_type == QueryPlanType::kSeqScan,
           "SELECT sin WHERE debe usar SeqScan fisico.");
    Expect(all_rows.metrics.buffer_misses > 0,
           "El primer scan fisico debe generar misses del buffer.");
    Expect(all_rows.metrics.disk_reads > 0,
           "El primer scan fisico debe leer paginas desde disco.");

    bool ordered = true;
    for (int index = 0; index < kTupleCount; ++index) {
      if (all_rows.rows[static_cast<std::size_t>(index)].key != index + 1 ||
          all_rows.rows[static_cast<std::size_t>(index)].value !=
              (index + 1) * 10) {
        ordered = false;
        break;
      }
    }
    Expect(ordered,
           "SeqScan fisico debe conservar orden y contenido.");

    const std::vector<Tuple> indexed =
        executor.Execute("SELECT * FROM registros WHERE id = 512;");
    Expect(indexed.size() == 1 && indexed[0].key == 512 &&
               indexed[0].value == 5120,
           "El indice persistido debe resolver claves tras el reinicio.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kIndexScan,
           "La busqueda persistida debe usar IndexScan.");

    const std::vector<Tuple> filtered =
        executor.Execute("SELECT * FROM registros WHERE value = 1230;");
    Expect(filtered.size() == 1 && filtered[0].key == 123,
           "Filter + SeqScan debe operar sobre paginas fisicas.");
    Expect(executor.GetLastPlanType() ==
               QueryPlanType::kFilteredSeqScan,
           "El filtro por value debe usar el scan fisico.");

    Expect(bpm.GetPageCount() == pages_before_open,
           "Las consultas de lectura no deben crear paginas.");
  }

  std::remove(db_file.c_str());
}

void TestLegacyCatalogMigration() {
  const std::string db_file = "test_legacy_catalog.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(6);
    BufferPoolManager bpm(6, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable legacy_index(&bpm);
    for (int key = 1; key <= 3; ++key) {
      const std::optional<RID> rid =
          table_heap.InsertTuple(key, key * 100);
      Expect(rid.has_value() &&
                 legacy_index.Insert(key, key * 100),
             "Debe preparar el formato de indice anterior.");
    }
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(db_file);
    char page[PAGE_SIZE];
    disk_manager.read_page(CATALOG_PAGE_ID, page);
    const uint32_t legacy_version = LEGACY_CATALOG_VERSION;
    std::memcpy(page + sizeof(uint32_t), &legacy_version,
                sizeof(legacy_version));
    disk_manager.write_page(CATALOG_PAGE_ID, page);
  }

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(6);
    BufferPoolManager bpm(6, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable rebuilt_index(&bpm);

    Expect(rebuilt_index.IsNewlyCreated(),
           "La migracion debe invalidar solo el indice antiguo.");
    Expect(table_heap.GetTupleCount() == 3,
           "La migracion debe conservar TableHeap.");
    for (const LocatedRecord &record : table_heap.ReadAll()) {
      Expect(rebuilt_index.Insert(record.key, record.rid.Encode()),
             "Debe reconstruir key -> RID desde TableHeap.");
    }

    QueryExecutor executor("registros", &table_heap, &rebuilt_index);
    const std::vector<Tuple> rows =
        executor.Execute("SELECT * FROM registros WHERE id = 2;");
    Expect(rows.size() == 1 && rows[0].value == 200,
           "El indice migrado debe recuperar la fila fisica.");
  }

  std::remove(db_file.c_str());
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE PERSISTENCIA FISICA ===\n";

  TestPhysicalTableAndPersistentIndex();
  TestLegacyCatalogMigration();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de persistencia pasaron correctamente.\n";
  return 0;
}
