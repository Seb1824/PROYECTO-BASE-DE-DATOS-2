#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "query/cli.h"
#include "query/query_executor.h"
#include "query/query_profiler.h"
#include "query/seq_scan_operator.h"
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

  const std::string db_file = "mini_sgbd.db";

  int exit_code = 0;

  try {
    {
      DiskManager disk_manager(db_file);
      CARReplacer replacer(10);
      BufferPoolManager bpm(10, &disk_manager, &replacer);
      TableHeap table_heap(&bpm);
      ExtensibleHashTable hash_index(&bpm);

      const std::vector<Tuple> seed_tuples = {
          Tuple{101, 505},
          Tuple{102, 510},
          Tuple{103, 515},
          Tuple{104, 520},
          Tuple{105, 525},
      };

      if (table_heap.GetTupleCount() == 0) {
        if (!hash_index.IsNewlyCreated()) {
          throw std::runtime_error(
              "El catalogo contiene un indice sin tabla asociada.");
        }

        for (const Tuple &tuple : seed_tuples) {
          if (!table_heap.Insert(tuple.key, tuple.value) ||
              !hash_index.Insert(tuple.key, tuple.value)) {
            throw std::runtime_error(
                "No se pudo inicializar la tabla persistente.");
          }
        }
      } else if (hash_index.IsNewlyCreated()) {
        SeqScanOperator scan(&table_heap);
        scan.Open();
        Tuple tuple;
        while (scan.Next(&tuple)) {
          if (!hash_index.Insert(tuple.key, tuple.value)) {
            scan.Close();
            throw std::runtime_error(
                "No se pudo reconstruir el indice persistente.");
          }
        }
        scan.Close();
      }

      QueryExecutor executor("registros", &table_heap, &hash_index);
      QueryProfiler profiler(&executor, &bpm, &disk_manager);

      PrintHeader();
      std::cout << "[INFO] Tabla 'registros' inicializada con "
                << table_heap.GetTupleCount() << " filas persistentes.\n";
      std::cout << "[INFO] Indice hash disponible para las columnas key/id.\n";
      std::cout << "[INFO] Archivo: " << db_file << "\n";

      exit_code = RunCli(std::cin, std::cout, &profiler);
      bpm.FlushAllPages();
    }
  } catch (const std::exception &error) {
    std::cerr << "[ERROR FATAL] " << error.what() << '\n';
    exit_code = 1;
  }

  return exit_code;
}
