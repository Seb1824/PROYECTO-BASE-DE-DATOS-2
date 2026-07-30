#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
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
#include "storage/table_heap.h"

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
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Arequipa", "Medico"},
      Tuple{3, "Carla", "Arequipa", "Arquitecta"},
  };
}

bool HasTimelinePhase(const ProfiledQueryResult &result,
                      const std::string &phase) {
  for (const TimelineEvent &event : result.trace.timeline) {
    if (event.phase == phase) {
      return true;
    }
  }
  return false;
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
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);

    for (const Tuple &tuple : BuildTuples()) {
      const std::optional<RID> rid =
          table_heap.InsertTuple(tuple);
      Expect(rid.has_value() &&
                 hash_index.Insert(tuple.id, rid->Encode()),
             "Debe inicializar el indice para el profiler.");
    }

    QueryExecutor executor("personas", &table_heap, &hash_index);
    QueryProfiler profiler(&executor, &bpm, &disk_manager);
    profiler.SetTracingEnabled(true);

    const ProfiledQueryResult index_result =
        profiler.Execute("SELECT * FROM personas WHERE id = 2;");

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
    Expect(index_result.trace.operators.size() == 1 &&
               index_result.trace.operators[0].name == "IndexScan",
           "El perfil debe registrar el nodo fisico IndexScan.");
    Expect(HasTimelinePhase(index_result, "Open") &&
               HasTimelinePhase(index_result, "Next") &&
               HasTimelinePhase(index_result, "Close"),
           "El perfil debe capturar la linea temporal Volcano.");
    Expect(!index_result.trace.car_events.empty(),
           "La consulta fisica debe capturar estados CAR.");

    const ProfiledQueryResult sequential_result =
        profiler.Execute(
            "SELECT * FROM personas WHERE ciudad = 'Arequipa';");

    Expect(sequential_result.rows.size() == 2,
           "La consulta filtrada debe devolver dos filas.");
    Expect(sequential_result.plan_type == QueryPlanType::kFilteredSeqScan,
           "La consulta por ciudad debe registrar escaneo filtrado.");
    Expect(sequential_result.metrics.buffer_hits > 0,
           "El scan fisico debe registrar accesos al Buffer Pool.");
    Expect(sequential_result.metrics.io_operations == 0,
           "Las paginas calientes no deben producir I/O adicional.");
    Expect(sequential_result.trace.operators.size() == 2 &&
               sequential_result.trace.operators[0].name == "Filter" &&
               sequential_result.trace.operators[1].name == "SeqScan",
           "El perfil debe conservar la jerarquia Filter -> SeqScan.");
    Expect(sequential_result.trace.operators[1].rows_out == 3,
           "SeqScan debe contabilizar todas las filas producidas.");
    Expect(sequential_result.trace.operators[0].rows_out == 2,
           "Filter debe contabilizar solo las filas filtradas.");
  }

  std::remove(db_file.c_str());
}

void TestProfilerWithoutStorageMetrics() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("personas", tuples);
  QueryProfiler profiler(&executor);
  profiler.SetTracingEnabled(true);

  const ProfiledQueryResult result =
      profiler.Execute("SELECT * FROM personas;");

  Expect(result.rows.size() == tuples.size(),
         "El profiler debe funcionar sin fuentes de metricas.");
  Expect(result.metrics.buffer_hits == 0 &&
             result.metrics.buffer_misses == 0 &&
             result.metrics.io_operations == 0,
         "Las metricas opcionales deben quedar en cero.");
  Expect(result.trace.operators.size() == 1 &&
             result.trace.operators[0].name == "SeqScan",
         "El perfil en memoria tambien debe incluir el plan fisico.");
  Expect(result.trace.car_events.empty(),
         "Sin Buffer Pool no deben inventarse estados CAR.");
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
