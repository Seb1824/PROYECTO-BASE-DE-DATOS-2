#include <cstdio>
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
#include "query/tuple.h"
#include "storage/disk_manager.h"

namespace {

void PrintHeader() {
  std::cout << "\n============================================\n";
  std::cout << "        MINI-SGBD: CLI DE CONSULTAS\n";
  std::cout << "============================================\n";
}

}  // namespace

int main() {
  using namespace minisgbd;

  const std::string db_file = "mini_sgbd_cli.db";
  std::remove(db_file.c_str());

  int exit_code = 0;

  try {
    {
      DiskManager disk_manager(db_file);
      CARReplacer replacer(10);
      BufferPoolManager bpm(10, &disk_manager, &replacer);
      ExtensibleHashTable hash_index(&bpm);

      const std::vector<Tuple> tuples = {
          Tuple{101, 505},
          Tuple{102, 510},
          Tuple{103, 515},
          Tuple{104, 520},
          Tuple{105, 525},
      };

      for (const Tuple &tuple : tuples) {
        if (!hash_index.Insert(tuple.key, tuple.value)) {
          throw std::runtime_error(
              "No se pudo inicializar el indice de la CLI.");
        }
      }

      QueryExecutor executor("registros", tuples, &hash_index);
      QueryProfiler profiler(&executor, &bpm, &disk_manager);

      PrintHeader();
      std::cout << "[INFO] Tabla 'registros' inicializada con "
                << tuples.size() << " filas.\n";
      std::cout << "[INFO] Indice hash disponible para las columnas key/id.\n";

      exit_code = RunCli(std::cin, std::cout, &profiler);
      bpm.FlushAllPages();
    }
  } catch (const std::exception &error) {
    std::cerr << "[ERROR FATAL] " << error.what() << '\n';
    exit_code = 1;
  }

  std::remove(db_file.c_str());
  return exit_code;
}
