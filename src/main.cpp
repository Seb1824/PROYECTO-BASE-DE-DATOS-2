#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "query/cli.h"
#include "query/query_executor.h"
#include "query/query_profiler.h"
#include "query/tuple.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"

namespace {

void PrintHeader() {
  std::cout << "\n============================================\n";
  std::cout << "        MINI-SGBD: CLI DE CONSULTAS\n";
  std::cout << "============================================\n";
}

}  // namespace

int main() {
  using namespace minisgbd;

  const std::string db_file = "personas_sgbd.db";

  int exit_code = 0;

  try {
    {
      DiskManager disk_manager(db_file);
      CARReplacer replacer(10);
      BufferPoolManager bpm(10, &disk_manager, &replacer);
      TableHeap table_heap(&bpm);
      ExtensibleHashTable hash_index(&bpm);

      const std::vector<Tuple> seed_tuples = {
          Tuple{101, "Ana Torres", "Arequipa", "Ingeniera"},
          Tuple{102, "Luis Mendoza", "Lima", "Medico"},
          Tuple{103, "Carla Rojas", "Cusco", "Arquitecta"},
          Tuple{104, "Diego Salazar", "Arequipa",
                "Analista de Datos"},
          Tuple{105, "Sofia Vargas", "Trujillo", "Abogada"},
      };

      if (table_heap.GetFirstPageId() == INVALID_PAGE_ID) {
        if (!hash_index.IsNewlyCreated()) {
          throw std::runtime_error(
              "El catalogo contiene un indice sin tabla asociada.");
        }

        for (const Tuple &tuple : seed_tuples) {
          const std::optional<RID> rid =
              table_heap.InsertTuple(tuple);
          if (!rid.has_value() ||
              !hash_index.Insert(tuple.id, rid->Encode())) {
            throw std::runtime_error(
                "No se pudo inicializar la tabla persistente.");
          }
        }
      } else if (hash_index.IsNewlyCreated()) {
        for (const LocatedRecord &record : table_heap.ReadAll()) {
          if (!hash_index.Insert(
                  record.person.id, record.rid.Encode())) {
            throw std::runtime_error(
                "No se pudo reconstruir el indice persistente.");
          }
        }
      }

      QueryExecutor executor("personas", &table_heap, &hash_index);
      QueryProfiler profiler(&executor, &bpm, &disk_manager);
      profiler.SetVisualizationPath("build/query_profile.html");

      PrintHeader();
      std::cout << "[INFO] Tabla 'personas' inicializada con "
                << table_heap.GetTupleCount() << " filas persistentes.\n";
      std::cout << "[INFO] Indice hash persistente disponible para id.\n";
      std::cout << "[INFO] Archivo: " << db_file << "\n";
      std::cout << "[INFO] Perfil visual: build/query_profile.html\n";

      exit_code = RunCli(std::cin, std::cout, &profiler);
      bpm.FlushAllPages();
    }
  } catch (const std::exception &error) {
    std::cerr << "[ERROR FATAL] " << error.what() << '\n';
    exit_code = 1;
  }

  return exit_code;
}
