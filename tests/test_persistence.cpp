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

PersonRecord BuildPerson(int id) {
  return PersonRecord{
      id,
      "Persona " + std::to_string(id),
      id % 2 == 0 ? "Lima" : "Arequipa",
      id % 3 == 0 ? "Ingeniera" : "Analista"};
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

    for (int id = 1; id <= kTupleCount; ++id) {
      const PersonRecord person = BuildPerson(id);
      const std::optional<RID> rid =
          table_heap.InsertTuple(person);
      Expect(rid.has_value(),
             "Debe insertar cada registro en paginas fisicas.");
      Expect(rid.has_value() &&
                 hash_index.Insert(id, rid->Encode()),
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

    QueryExecutor executor("personas", &table_heap, &hash_index);
    QueryProfiler profiler(&executor, &bpm, &disk_manager);

    const ProfiledQueryResult all_rows =
        profiler.Execute("SELECT * FROM personas;");
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
      if (all_rows.rows[static_cast<std::size_t>(index)] !=
          BuildPerson(index + 1)) {
        ordered = false;
        break;
      }
    }
    Expect(ordered,
           "SeqScan fisico debe conservar orden y contenido.");

    const std::vector<Tuple> indexed =
        executor.Execute("SELECT * FROM personas WHERE id = 80;");
    Expect(indexed.size() == 1 && indexed[0] == BuildPerson(80),
           "El indice persistido debe resolver ids tras el reinicio.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kIndexScan,
           "La busqueda persistida debe usar IndexScan.");

    const std::vector<Tuple> filtered =
        executor.Execute(
            "SELECT * FROM personas WHERE nombre = 'Persona 73';");
    Expect(filtered.size() == 1 && filtered[0] == BuildPerson(73),
           "Filter + SeqScan debe operar sobre paginas fisicas.");
    Expect(executor.GetLastPlanType() ==
               QueryPlanType::kFilteredSeqScan,
           "El filtro por nombre debe usar el scan fisico.");

    Expect(bpm.GetPageCount() == pages_before_open,
           "Las consultas de lectura no deben crear paginas.");
  }

  std::remove(db_file.c_str());
}

void TestIncompatibleCatalogVersion() {
  const std::string db_file = "test_incompatible_catalog.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(6);
    BufferPoolManager bpm(6, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    const PersonRecord person = BuildPerson(1);
    const std::optional<RID> rid = table_heap.InsertTuple(person);
    Expect(rid.has_value() &&
               hash_index.Insert(person.id, rid->Encode()),
           "Debe preparar un catalogo valido.");
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(db_file);
    char page[PAGE_SIZE];
    disk_manager.read_page(CATALOG_PAGE_ID, page);
    const uint32_t incompatible_version = 2;
    std::memcpy(page + sizeof(uint32_t), &incompatible_version,
                sizeof(incompatible_version));
    disk_manager.write_page(CATALOG_PAGE_ID, page);
  }

  bool rejected = false;
  {
    try {
      DiskManager disk_manager(db_file);
      CARReplacer replacer(6);
      BufferPoolManager bpm(6, &disk_manager, &replacer);
      TableHeap table_heap(&bpm);
      (void)table_heap;
    } catch (const std::runtime_error &error) {
      rejected =
          std::string(error.what()).find("incompatible") !=
          std::string::npos;
    }
  }
  Expect(rejected,
         "El catalogo v2 debe rechazarse sin reinterpretar sus paginas.");

  std::remove(db_file.c_str());
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE PERSISTENCIA FISICA ===\n";

  TestPhysicalTableAndPersistentIndex();
  TestIncompatibleCatalogVersion();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de persistencia pasaron correctamente.\n";
  return 0;
}
