#include <cstdio>
#include <functional>
#include <iostream>
#include <limits>
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

using namespace minisgbd;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

void ExpectInvalidArgument(const std::function<void()> &action,
                           const std::string &message) {
  try {
    action();
    Expect(false, message);
  } catch (const std::invalid_argument &) {
    // Resultado esperado.
  } catch (const std::exception &error) {
    Expect(false, message + " Error inesperado: " + error.what());
  }
}

std::vector<Tuple> BuildTuples() {
  return {
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Arequipa", "Medico"},
      Tuple{3, "Carla", "Arequipa", "Arquitecta"},
      Tuple{4, "Diego", "Cusco", "Analista"},
  };
}

void LoadPhysicalRows(TableHeap *table_heap,
                      ExtensibleHashTable *hash_index) {
  for (const Tuple &tuple : BuildTuples()) {
    const std::optional<RID> rid =
        table_heap->InsertTuple(tuple);
    Expect(rid.has_value() &&
               hash_index->Insert(tuple.id, rid->Encode()),
           "Debe cargar cada persona en tabla e indice.");
  }
}

void TestSequentialPlansAndProjection() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("personas", tuples);

  const std::vector<Tuple> all =
      executor.Execute("SELECT * FROM personas;");
  Expect(all == tuples,
         "SeqScan debe conservar todas las personas y su orden.");
  Expect(executor.GetLastPlanType() == QueryPlanType::kSeqScan,
         "SELECT sin WHERE debe usar SeqScan.");

  const std::vector<Tuple> filtered = executor.Execute(
      "SELECT * FROM personas WHERE ciudad = 'Arequipa';");
  Expect(filtered.size() == 2 && filtered[0].id == 2 &&
             filtered[1].id == 3,
         "El filtro de texto debe producir ambas coincidencias.");
  Expect(executor.GetLastPlanType() ==
             QueryPlanType::kFilteredSeqScan,
         "El filtro de ciudad debe usar Filter + SeqScan.");

  const std::vector<Tuple> projected = executor.Execute(
      "SELECT nombre, profesion FROM personas "
      "WHERE ciudad = 'Arequipa';");
  Expect(projected.size() == 2 &&
             projected[0].id == 0 &&
             projected[0].nombre == "Bruno" &&
             projected[0].ciudad.empty() &&
             projected[0].profesion == "Medico",
         "Projection debe ocultar columnas no seleccionadas.");
  Expect(executor.GetLastOutputColumns() ==
             std::vector<std::string>({"nombre", "profesion"}),
         "Debe conservar el orden de salida solicitado.");

  const std::vector<Tuple> comparison = executor.Execute(
      "SELECT * FROM personas WHERE nombre >= 'Carla';");
  Expect(comparison.size() == 2 &&
             comparison[0].nombre == "Carla" &&
             comparison[1].nombre == "Diego",
         "Los comparadores deben operar lexicograficamente en texto.");
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
    LoadPhysicalRows(&table_heap, &hash_index);

    QueryExecutor executor("personas", &table_heap, &hash_index);
    const std::vector<Tuple> indexed =
        executor.Execute("SELECT * FROM personas WHERE id = 2;");
    Expect(indexed.size() == 1 && indexed[0] == BuildTuples()[1],
           "IndexScan debe recuperar la persona completa.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kIndexScan,
           "La igualdad por id debe usar IndexScan.");

    const std::vector<Tuple> by_city = executor.Execute(
        "SELECT * FROM personas WHERE ciudad = 'Arequipa';");
    Expect(by_city.size() == 2,
           "El filtro de ciudad debe funcionar con indice disponible.");
    Expect(executor.GetLastPlanType() ==
               QueryPlanType::kFilteredSeqScan,
           "El indice de id no debe usarse para ciudad.");

    Expect(executor.Execute(
               "SELECT * FROM personas WHERE id = 999;")
               .empty(),
           "Un id inexistente no debe producir resultados.");
  }
  std::remove(db_file.c_str());
}

void TestPersistentMutations() {
  const std::string db_file = "test_query_executor_mutations.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("personas", &table_heap, &hash_index);

    const std::vector<Tuple> inserted = executor.Execute(
        "INSERT INTO personas VALUES "
        "(106, 'Ana Torres', 'Arequipa', 'Ingeniera');");
    Expect(inserted.size() == 1 &&
               inserted[0] ==
                   Tuple{106, "Ana Torres", "Arequipa", "Ingeniera"},
           "INSERT debe devolver la persona completa.");
    Expect(executor.GetLastPlanType() == QueryPlanType::kInsert &&
               table_heap.GetTupleCount() == 1,
           "INSERT debe persistir la fila e informar su plan.");

    ExpectInvalidArgument(
        [&executor]() {
          executor.Execute(
              "INSERT INTO personas VALUES "
              "(106, 'Duplicada', 'Lima', 'Medica');");
        },
        "INSERT debe rechazar ids duplicados.");
    Expect(table_heap.GetTupleCount() == 1,
           "Un duplicado no debe modificar TableHeap.");

    const std::vector<Tuple> minimum_id = executor.Execute(
        "INSERT INTO personas VALUES "
        "(-2147483648, 'Id Minimo', 'Puno', 'Investigadora');");
    Expect(minimum_id.size() == 1 &&
               minimum_id[0].id == std::numeric_limits<int>::min(),
           "INT_MIN debe ser un id valido con el indicador explicito.");
    executor.Execute(
        "DELETE FROM personas WHERE id = -2147483648;");

    InsertQuery oversized{
        "personas", 108, std::string(64, 'N'), "Lima", "Prueba"};
    ExpectInvalidArgument(
        [&executor, &oversized]() {
          executor.Execute(oversized);
        },
        "Debe rechazar textos que no caben en el registro fisico.");
    Expect(table_heap.GetTupleCount() == 1,
           "Un texto demasiado largo no debe modificar TableHeap.");

    const std::vector<Tuple> updated = executor.Execute(
        "UPDATE personas SET profesion = 'Arquitecta' "
        "WHERE id = 106;");
    Expect(updated.size() == 1 &&
               updated[0].profesion == "Arquitecta",
           "UPDATE debe modificar un campo de texto.");

    executor.Execute(
        "INSERT INTO personas VALUES "
        "(107, 'Luis Mendoza', 'Lima', 'Medico');");
    const std::vector<Tuple> rekeyed = executor.Execute(
        "UPDATE personas SET id = 206 WHERE id = 106;");
    Expect(rekeyed.size() == 1 && rekeyed[0].id == 206,
           "UPDATE debe cambiar un id unico.");
    Expect(executor.Execute(
               "SELECT * FROM personas WHERE id = 106;")
               .empty(),
           "El id anterior debe desaparecer del indice.");
    Expect(executor.Execute(
               "SELECT * FROM personas WHERE id = 206;")
               .size() == 1,
           "El nuevo id debe conservar el RID.");

    ExpectInvalidArgument(
        [&executor]() {
          executor.Execute(
              "UPDATE personas SET id = 107 WHERE id = 206;");
        },
        "UPDATE debe rechazar un id duplicado.");

    const std::vector<Tuple> deleted = executor.Execute(
        "DELETE FROM personas WHERE ciudad = 'Lima';");
    Expect(deleted.size() == 1 && deleted[0].id == 107,
           "DELETE debe filtrar por texto.");
    Expect(table_heap.GetTupleCount() == 1,
           "DELETE debe actualizar el conteo fisico.");
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("personas", &table_heap, &hash_index);

    const std::vector<Tuple> persisted = executor.Execute(
        "SELECT * FROM personas WHERE id = 206;");
    Expect(persisted.size() == 1 &&
               persisted[0].profesion == "Arquitecta",
           "INSERT y UPDATE deben persistir tras reiniciar.");
    Expect(executor.Execute(
               "SELECT * FROM personas WHERE id = 107;")
               .empty(),
           "DELETE debe persistir tras reiniciar.");
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
    QueryExecutor executor("personas", &table_heap, &hash_index);

    constexpr int shared_low_bits = 512;
    for (int index = 0; index < BUCKET_ARRAY_SIZE; ++index) {
      const int id = index * shared_low_bits;
      const PersonRecord person{
          id, "Persona " + std::to_string(index),
          "Lima", "Prueba"};
      const std::optional<RID> rid =
          table_heap.InsertTuple(person);
      Expect(rid.has_value() &&
                 hash_index.Insert(id, rid->Encode()),
             "Debe preparar el bucket para probar rollback.");
    }

    const uint32_t count_before = table_heap.GetTupleCount();
    bool overflow_detected = false;
    try {
      executor.Execute(
          "INSERT INTO personas VALUES (" +
          std::to_string(BUCKET_ARRAY_SIZE * shared_low_bits) +
          ", 'Overflow', 'Lima', 'Prueba');");
    } catch (const std::overflow_error &) {
      overflow_detected = true;
    }
    Expect(overflow_detected,
           "La prueba debe alcanzar el limite del directorio.");
    Expect(table_heap.GetTupleCount() == count_before &&
               table_heap.ReadAll().size() == count_before,
           "Un fallo del indice debe revertir el INSERT fisico.");
  }
  std::remove(db_file.c_str());
}

void TestBulkUpdateDeleteAndValidation() {
  const std::string db_file = "test_query_executor_bulk.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(8);
    BufferPoolManager bpm(8, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("personas", &table_heap, &hash_index);

    executor.Execute(
        "INSERT INTO personas VALUES (1, 'Ana', 'Lima', 'A');");
    executor.Execute(
        "INSERT INTO personas VALUES (2, 'Bruno', 'Cusco', 'B');");
    executor.Execute(
        "INSERT INTO personas VALUES (3, 'Carla', 'Piura', 'C');");

    const std::vector<Tuple> updated = executor.Execute(
        "UPDATE personas SET profesion = 'Especialista';");
    Expect(updated.size() == 3,
           "UPDATE sin WHERE debe afectar todas las filas.");

    const std::vector<Tuple> partial_delete =
        executor.Execute("DELETE FROM personas WHERE id < 3;");
    Expect(partial_delete.size() == 2,
           "DELETE debe admitir comparadores no indexados.");
    const std::vector<Tuple> remaining =
        executor.Execute("SELECT * FROM personas;");
    Expect(remaining.size() == 1 && remaining[0].id == 3 &&
               remaining[0].profesion == "Especialista",
           "Los registros borrados no deben aparecer en SeqScan.");

    ExpectInvalidArgument(
        [&executor]() {
          executor.Execute(
              "SELECT * FROM personas WHERE id = 'tres';");
        },
        "id debe rechazar un literal de texto.");
    ExpectInvalidArgument(
        [&executor]() {
          executor.Execute(
              "SELECT * FROM personas WHERE ciudad = 10;");
        },
        "ciudad debe rechazar un literal entero.");

    const std::vector<Tuple> final_delete =
        executor.Execute("DELETE FROM personas;");
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
    QueryExecutor executor("personas", &table_heap, &hash_index);
    Expect(table_heap.GetTupleCount() == 0 &&
               table_heap.GetFirstPageId() != INVALID_PAGE_ID,
           "Una tabla vaciada debe conservar su cadena fisica.");
    Expect(executor.Execute("SELECT * FROM personas;").empty(),
           "DELETE total debe persistir tras reiniciar.");
  }
  std::remove(db_file.c_str());
}

void TestInvalidConfiguration() {
  const std::vector<Tuple> tuples = BuildTuples();
  QueryExecutor executor("personas", tuples);

  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute("SELECT * FROM otra_tabla;");
      },
      "Debe rechazar una tabla desconocida.");
  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute(
            "SELECT desconocida FROM personas;");
      },
      "Debe rechazar una columna desconocida.");
  ExpectInvalidArgument(
      [&executor]() {
        executor.Execute(
            "INSERT INTO personas VALUES "
            "(5, 'E', 'Lima', 'P');");
      },
      "INSERT debe rechazar un ejecutor solo en memoria.");
  ExpectInvalidArgument(
      [&tuples]() {
        QueryExecutor executor("", tuples);
      },
      "Debe rechazar un nombre de tabla vacio.");
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE QUERY EXECUTOR ===\n";
  TestSequentialPlansAndProjection();
  TestIndexPlan();
  TestPersistentMutations();
  TestInsertRollbackWhenIndexFails();
  TestBulkUpdateDeleteAndValidation();
  TestInvalidConfiguration();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }
  std::cout << "Todas las pruebas de QueryExecutor pasaron correctamente.\n";
  return 0;
}
