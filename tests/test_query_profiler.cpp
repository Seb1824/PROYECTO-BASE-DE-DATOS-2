#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "query/query_executor.h"
#include "query/query_profiler.h"
#include "query/tuple.h"
#include "storage/disk_manager.h"

using namespace minisgbd;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

std::vector<Tuple> BuildTuples() {
  return {
      Tuple{1, 100},
      Tuple{2, 200},
      Tuple{3, 200},
  };
}

void TestDiskIoCounters() {
  const std::string db_file = "test_disk_counters.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    char page[PAGE_SIZE];
    std::memset(page, 0, PAGE_SIZE);

    Expect(disk_manager.GetReadCount() == 0,
           "El contador de lecturas debe iniciar en cero.");
    Expect(disk_manager.GetWriteCount() == 0,
           "El contador de escrituras debe iniciar en cero.");

    const page_id_t page_id = disk_manager.allocate_page();
    Expect(disk_manager.GetWriteCount() == 1,
           "allocate_page debe contar una escritura de pagina.");

    disk_manager.read_page(page_id, page);
    Expect(disk_manager.GetReadCount() == 1,
           "read_page debe contar una lectura de pagina.");

    disk_manager.write_page(page_id, page);
    Expect(disk_manager.GetWriteCount() == 2,
           "write_page debe incrementar las escrituras.");
  }

  std::remove(db_file.c_str());
}

void TestProfiledIndexAndSequentialQueries() {
  const std::string db_file = "test_query_profiler.db";
  std::remove(db_file.c_str());

  {
    const std::vector<Tuple> tuples = BuildTuples();
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    ExtensibleHashTable hash_index(&bpm);

    for (const Tuple &tuple : tuples) {
      Expect(hash_index.Insert(tuple.key, tuple.value),
             "Debe inicializar el indice para el profiler.");
    }

    QueryExecutor executor("registros", tuples, &hash_index);
    QueryProfiler profiler(&executor, &bpm, &disk_manager);

    const ProfiledQueryResult index_result =
        profiler.Execute("SELECT * FROM registros WHERE id = 2;");

    Expect(index_result.rows.size() == 1,
           "La consulta indexada debe devolver una fila.");
    Expect(index_result.plan_type == QueryPlanType::kIndexScan,
           "La consulta por id debe registrar IndexScan.");
    Expect(index_result.metrics.elapsed_ms >= 0.0,
           "El tiempo medido no puede ser negativo.");
    Expect(index_result.metrics.buffer_hits > 0,
           "El IndexScan debe registrar accesos al buffer.");
    Expect(index_result.metrics.buffer_hit_ratio >= 0.0 &&
               index_result.metrics.buffer_hit_ratio <= 1.0,
           "El hit ratio debe estar entre cero y uno.");
    Expect(index_result.metrics.io_operations ==
               index_result.metrics.disk_reads +
                   index_result.metrics.disk_writes,
           "El costo de I/O debe ser lecturas mas escrituras.");

    const ProfiledQueryResult sequential_result =
        profiler.Execute("SELECT * FROM registros WHERE value = 200;");

    Expect(sequential_result.rows.size() == 2,
           "La consulta filtrada debe devolver dos filas.");
    Expect(sequential_result.plan_type == QueryPlanType::kFilteredSeqScan,
           "La consulta por value debe registrar escaneo filtrado.");
    Expect(sequential_result.metrics.buffer_hits == 0 &&
               sequential_result.metrics.buffer_misses == 0,
           "El scan actual en memoria no debe heredar métricas anteriores.");
    Expect(sequential_result.metrics.io_operations == 0,
           "El scan actual en memoria no debe reportar I/O de disco.");
  }

  std::remove(db_file.c_str());
}

void TestProfilerWithoutStorageMetrics() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("registros", tuples);
  QueryProfiler profiler(&executor);

  const ProfiledQueryResult result =
      profiler.Execute("SELECT * FROM registros;");

  Expect(result.rows.size() == tuples.size(),
         "El profiler debe funcionar sin fuentes de metricas.");
  Expect(result.metrics.buffer_hits == 0 &&
             result.metrics.buffer_misses == 0 &&
             result.metrics.io_operations == 0,
         "Las metricas opcionales deben quedar en cero.");
}

void TestInvalidProfiler() {
  try {
    QueryProfiler profiler(nullptr);
    Expect(false, "Debe rechazar un QueryExecutor nulo.");
  } catch (const std::invalid_argument &) {
    // Resultado esperado.
  }
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE QUERY PROFILER ===\n";

  TestDiskIoCounters();
  TestProfiledIndexAndSequentialQueries();
  TestProfilerWithoutStorageMetrics();
  TestInvalidProfiler();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de QueryProfiler pasaron correctamente.\n";
  return 0;
}
