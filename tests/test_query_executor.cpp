#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "index/hash_bucket_page.h"
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

void TestProjectionAndComparisonOperators() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("registros", tuples);

  const std::vector<Tuple> projected = executor.Execute(
      "SELECT key FROM registros WHERE value >= 200;");
  Expect(projected.size() == 3,
         "El comparador >= debe devolver tres filas.");
  Expect(projected.size() == 3 && projected[0].key == 2 &&
             projected[1].key == 3 && projected[2].key == 4,
         "La proyeccion debe conservar las claves esperadas.");
  Expect(projected.size() == 3 && projected[0].value == 0,
         "ProjectionOperator debe ocultar la columna no seleccionada.");
  Expect(executor.GetLastOutputColumns().size() == 1 &&
             executor.GetLastOutputColumns()[0] == "key",
         "El ejecutor debe exponer el esquema proyectado.");

  const std::vector<Tuple> not_equal = executor.Execute(
      "SELECT * FROM registros WHERE id != 2;");
  Expect(not_equal.size() == 3,
         "El comparador != debe excluir la clave indicada.");

  const std::vector<Tuple> less = executor.Execute(
      "SELECT value FROM registros WHERE key < 3;");
  Expect(less.size() == 2 && less[0].key == 0 &&
             less[0].value == 100,
         "La proyeccion de value debe ocultar key.");

  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute("SELECT desconocida FROM registros;");
      },
      "Debe rechazar una columna de proyeccion desconocida.");
}

void TestIndexPlan() {
  const std::string db_file = "test_query_executor.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);

    for (const Tuple &tuple : BuildTuples()) {
      const std::optional<minisgbd::RID> rid =
          table_heap.InsertTuple(tuple.key, tuple.value);
      Expect(rid.has_value() &&
                 hash_index.Insert(tuple.key, rid->Encode()),
             "Debe insertar los datos de prueba en el indice.");
    }

    QueryExecutor executor("registros", &table_heap, &hash_index);
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

    const std::vector<Tuple> updated = executor.Execute(
        "UPDATE registros SET value = 535 WHERE id = 106;");
    Expect(updated.size() == 1 && updated[0].value == 535,
           "UPDATE debe modificar el valor de una fila.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kUpdate,
           "UPDATE debe informar su tipo de plan.");

    executor.Execute("INSERT INTO registros VALUES (107, 540);");
    const std::vector<Tuple> rekeyed = executor.Execute(
        "UPDATE registros SET key = 206 WHERE id = 106;");
    Expect(rekeyed.size() == 1 && rekeyed[0].key == 206 &&
               rekeyed[0].value == 535,
           "UPDATE debe poder cambiar una clave unica.");
    Expect(executor.Execute(
               "SELECT * FROM registros WHERE id = 106;")
               .empty(),
           "La clave anterior debe desaparecer del indice.");
    Expect(executor.Execute(
               "SELECT * FROM registros WHERE id = 206;")
               .size() == 1,
           "La nueva clave debe apuntar al mismo RID.");

    ExpectInvalidArgument(
        [&executor]() {
          executor.Execute(
              "UPDATE registros SET key = 107 WHERE id = 206;");
        },
        "UPDATE debe rechazar una clave duplicada.");

    const std::vector<Tuple> deleted = executor.Execute(
        "DELETE FROM registros WHERE value >= 540;");
    Expect(deleted.size() == 1 && deleted[0].key == 107,
           "DELETE debe eliminar las filas que cumplen la condicion.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kDelete,
           "DELETE debe informar su tipo de plan.");
    Expect(table_heap.GetTupleCount() == 1,
           "DELETE debe actualizar el conteo persistente.");
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
        executor.Execute("SELECT * FROM registros WHERE key = 206;");
    Expect(selected.size() == 1 && selected[0].value == 535,
           "UPDATE debe permanecer disponible despues de reiniciar.");
    Expect(executor.Execute(
               "SELECT * FROM registros WHERE key = 107;")
               .empty(),
           "DELETE debe permanecer aplicado despues de reiniciar.");
  }

  std::remove(db_file.c_str());
}

void TestInsertRollbackWhenIndexFails() {
  const std::string db_file = "test_query_executor_rollback.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("registros", &table_heap, &hash_index);

    constexpr int shared_low_bits = 512;
    for (int index = 0; index < minisgbd::BUCKET_ARRAY_SIZE; ++index) {
      const int key = index * shared_low_bits;
      const std::optional<minisgbd::RID> rid =
          table_heap.InsertTuple(key, index);
      Expect(rid.has_value() &&
                 hash_index.Insert(key, rid->Encode()),
             "Debe preparar el bucket para probar rollback.");
    }

    const uint32_t count_before = table_heap.GetTupleCount();
    bool overflow_detected = false;
    try {
      executor.Execute(
          "INSERT INTO registros VALUES (" +
          std::to_string(minisgbd::BUCKET_ARRAY_SIZE *
                         shared_low_bits) +
          ", 999);");
    } catch (const std::overflow_error &) {
      overflow_detected = true;
    }
    Expect(overflow_detected,
           "La prueba debe alcanzar el limite del directorio.");
    Expect(table_heap.GetTupleCount() == count_before,
           "Un fallo del indice debe revertir el INSERT fisico.");
    Expect(table_heap.ReadAll().size() == count_before,
           "El rollback no debe dejar una fila visible.");
  }

  std::remove(db_file.c_str());
}

void TestBulkUpdateAndDelete() {
  const std::string db_file = "test_query_executor_bulk.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(8);
    BufferPoolManager bpm(8, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("registros", &table_heap, &hash_index);

    executor.Execute("INSERT INTO registros VALUES (1, 10);");
    executor.Execute("INSERT INTO registros VALUES (2, 20);");
    executor.Execute("INSERT INTO registros VALUES (3, 30);");

    const std::vector<Tuple> updated =
        executor.Execute("UPDATE registros SET value = 999;");
    Expect(updated.size() == 3,
           "UPDATE sin WHERE debe afectar todas las filas.");

    const std::vector<Tuple> partial_delete =
        executor.Execute("DELETE FROM registros WHERE key < 3;");
    Expect(partial_delete.size() == 2,
           "DELETE debe admitir comparadores no indexados.");
    const std::vector<Tuple> remaining =
        executor.Execute("SELECT * FROM registros;");
    Expect(remaining.size() == 1 && remaining[0].key == 3 &&
               remaining[0].value == 999,
           "Los tombstones no deben aparecer en SeqScan.");

    const std::vector<Tuple> final_delete =
        executor.Execute("DELETE FROM registros;");
    Expect(final_delete.size() == 1 &&
               table_heap.GetTupleCount() == 0,
           "DELETE sin WHERE debe vaciar la tabla.");
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(8);
    BufferPoolManager bpm(8, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("registros", &table_heap, &hash_index);

    Expect(table_heap.GetTupleCount() == 0 &&
               table_heap.GetFirstPageId() !=
                   minisgbd::INVALID_PAGE_ID,
           "Una tabla vaciada debe conservar su cadena fisica.");
    Expect(executor.Execute("SELECT * FROM registros;").empty(),
           "DELETE total debe persistir despues de reiniciar.");
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
  TestProjectionAndComparisonOperators();
  TestIndexPlan();
  TestPersistentInsert();
  TestInsertRollbackWhenIndexFails();
  TestBulkUpdateAndDelete();
  TestInvalidQueries();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de QueryExecutor pasaron correctamente.\n";
  return 0;
}
