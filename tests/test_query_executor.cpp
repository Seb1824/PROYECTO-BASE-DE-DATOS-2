#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "query/query_executor.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"

using minisgbd::BufferPoolManager;
using minisgbd::CARReplacer;
using minisgbd::DiskManager;
using minisgbd::ExtensibleHashTable;
using minisgbd::QueryExecutor;
using minisgbd::QueryPlanType;
using minisgbd::TableHeap;
using minisgbd::Tuple;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

template <typename Function>
void ExpectInvalidArgument(Function function, const std::string &message) {
  try {
    function();
    Expect(false, message);
  } catch (const std::invalid_argument &) {
    // Resultado esperado.
  } catch (const std::exception &error) {
    Expect(false, message + " Error inesperado: " + error.what());
  }
}

std::vector<Tuple> BuildTuples() {
  return {
      Tuple{1, 100},
      Tuple{2, 200},
      Tuple{3, 200},
      Tuple{4, 400},
  };
}

void TestSequentialPlan() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("registros", tuples);

  const std::vector<Tuple> results =
      executor.Execute("SELECT * FROM registros;");

  Expect(results.size() == tuples.size(),
         "SELECT sin WHERE debe devolver todos los registros.");
  Expect(executor.GetLastPlanType() == QueryPlanType::kSeqScan,
         "SELECT sin WHERE debe usar SeqScan.");

  for (std::size_t index = 0;
       index < results.size() && index < tuples.size(); ++index) {
    Expect(results[index].key == tuples[index].key &&
               results[index].value == tuples[index].value,
           "SeqScan debe conservar registros y orden.");
  }
}

void TestFilteredSequentialPlan() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("registros", tuples);

  const std::vector<Tuple> results =
      executor.Execute("SELECT * FROM registros WHERE value = 200;");

  Expect(results.size() == 2,
         "El filtro por value debe devolver dos coincidencias.");
  if (results.size() == 2) {
    Expect(results[0].key == 2 && results[1].key == 3,
           "El filtro debe conservar el orden de las coincidencias.");
  }
  Expect(executor.GetLastPlanType() == QueryPlanType::kFilteredSeqScan,
         "Un filtro sin indice debe usar FilterOperator y SeqScan.");

  const std::vector<Tuple> key_results =
      executor.Execute("SELECT * FROM registros WHERE key = 4;");
  Expect(key_results.size() == 1 && key_results[0].value == 400,
         "Sin indice, el filtro por key debe seguir funcionando.");
  Expect(executor.GetLastPlanType() == QueryPlanType::kFilteredSeqScan,
         "Sin indice disponible, key debe usar escaneo filtrado.");
}

void TestIndexPlan() {
  const std::string db_file = "test_query_executor.db";
  std::remove(db_file.c_str());

  {
    const std::vector<Tuple> tuples = BuildTuples();
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    ExtensibleHashTable hash_index(&bpm);

    for (const Tuple &tuple : tuples) {
      Expect(hash_index.Insert(tuple.key, tuple.value),
             "Debe insertar los datos de prueba en el indice.");
    }

    QueryExecutor executor("registros", tuples, &hash_index);
    const std::vector<Tuple> results =
        executor.Execute("SELECT * FROM registros WHERE id = 2;");

    Expect(results.size() == 1,
           "El IndexScan debe devolver una coincidencia.");
    if (results.size() == 1) {
      Expect(results[0].key == 2 && results[0].value == 200,
             "El IndexScan debe devolver el Tuple indexado.");
    }
    Expect(executor.GetLastPlanType() == QueryPlanType::kIndexScan,
           "WHERE id con indice debe usar IndexScan.");

    const std::vector<Tuple> value_results =
        executor.Execute("SELECT * FROM registros WHERE value = 200;");
    Expect(value_results.size() == 2,
           "El filtro por value debe funcionar aunque exista un indice.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kFilteredSeqScan,
           "El indice de claves no debe usarse para filtrar por value.");

    const std::vector<Tuple> missing =
        executor.Execute("SELECT * FROM registros WHERE key = 999;");
    Expect(missing.empty(),
           "Una clave inexistente en el indice no debe producir resultados.");
  }

  std::remove(db_file.c_str());
}

void TestPersistentInsert() {
  const std::string db_file = "test_query_executor_insert.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("registros", &table_heap, &hash_index);

    const std::vector<Tuple> inserted =
        executor.Execute("INSERT INTO registros VALUES (106, 530);");
    Expect(inserted.size() == 1 && inserted[0].key == 106 &&
               inserted[0].value == 530,
           "INSERT debe devolver el registro insertado.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kInsert,
           "INSERT debe informar el tipo de plan correspondiente.");
    Expect(table_heap.GetTupleCount() == 1,
           "INSERT debe escribir una fila en TableHeap.");

    const std::vector<Tuple> selected =
        executor.Execute("SELECT * FROM registros WHERE id = 106;");
    Expect(selected.size() == 1 && selected[0].value == 530,
           "La fila insertada debe recuperarse mediante IndexScan.");

    ExpectInvalidArgument(
        [&executor]() {
          executor.Execute("INSERT INTO registros VALUES (106, 999);");
        },
        "INSERT debe rechazar claves duplicadas.");
    Expect(table_heap.GetTupleCount() == 1,
           "Un duplicado rechazado no debe modificar TableHeap.");
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("registros", &table_heap, &hash_index);

    const std::vector<Tuple> selected =
        executor.Execute("SELECT * FROM registros WHERE key = 106;");
    Expect(selected.size() == 1 && selected[0].value == 530,
           "INSERT debe permanecer disponible despues de reiniciar.");
  }

  std::remove(db_file.c_str());
}

void TestInvalidQueries() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("registros", tuples);

  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute("SELECT * FROM otra_tabla;");
      },
      "Debe rechazar una tabla desconocida.");

  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute("SELECT * FROM registros WHERE desconocida = 1;");
      },
      "Debe rechazar una columna desconocida.");

  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute("SELECT FROM registros;");
      },
      "Debe propagar errores de sintaxis del parser.");

  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute("INSERT INTO registros VALUES (5, 500);");
      },
      "INSERT debe rechazar un ejecutor basado solo en memoria.");

  ExpectInvalidArgument(
      [&tuples]() {
        QueryExecutor executor("", tuples);
      },
      "Debe rechazar un nombre de tabla vacio.");
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE QUERY EXECUTOR ===\n";

  TestSequentialPlan();
  TestFilteredSequentialPlan();
  TestIndexPlan();
  TestPersistentInsert();
  TestInvalidQueries();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de QueryExecutor pasaron correctamente.\n";
  return 0;
}
