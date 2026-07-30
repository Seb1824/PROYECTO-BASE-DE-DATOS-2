#include <cstdio>
#include <iostream>
#include <sstream>
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

using minisgbd::BufferPoolManager;
using minisgbd::CARReplacer;
using minisgbd::DiskManager;
using minisgbd::ExtensibleHashTable;
using minisgbd::QueryExecutor;
using minisgbd::QueryProfiler;
using minisgbd::RunCli;
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

void ExpectContains(const std::string &text, const std::string &expected,
                    const std::string &message) {
  Expect(text.find(expected) != std::string::npos, message);
}

void TestInteractiveSession() {
  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Arequipa", "Medico"},
      Tuple{3, "Carla", "Arequipa", "Arquitecta"},
  };
  QueryExecutor executor("personas", tuples);
  QueryProfiler profiler(&executor);

  std::istringstream input(
      "\n"
      "SELECT * FROM personas;\n"
      "SELECT * FROM personas WHERE ciudad = 'Arequipa';\n"
      "SELECT FROM personas;\n"
      "help\n"
      "exit\n");
  std::ostringstream output;

  const int result = RunCli(input, output, &profiler);
  const std::string session = output.str();

  Expect(result == 0, "La CLI debe finalizar con codigo cero.");
  ExpectContains(session, "Ana",
                 "SELECT debe imprimir la primera fila.");
  ExpectContains(session, "Filas: 3",
                 "SELECT sin WHERE debe informar tres filas.");
  ExpectContains(session, "Plan: SeqScan",
                 "Debe mostrar el plan de escaneo secuencial.");
  ExpectContains(session, "Filas: 2",
                 "El filtro debe informar dos coincidencias.");
  ExpectContains(session, "Plan: Filter + SeqScan",
                 "Debe mostrar el plan de filtrado.");
  ExpectContains(session, "Tiempo:",
                 "Debe mostrar el tiempo de ejecucion.");
  ExpectContains(session, "Buffer hits:",
                 "Debe mostrar los hits del buffer.");
  ExpectContains(session, "Buffer misses:",
                 "Debe mostrar los misses del buffer.");
  ExpectContains(session, "Hit ratio:",
                 "Debe mostrar el hit ratio.");
  ExpectContains(session, "Costo I/O:",
                 "Debe mostrar el costo de I/O.");
  ExpectContains(session, "[ERROR]",
                 "Una consulta invalida debe mostrar un error.");
  ExpectContains(session, "Comandos disponibles:",
                 "El comando help debe mostrar ayuda.");
  ExpectContains(session, "INSERT INTO personas VALUES",
                 "La ayuda debe documentar INSERT.");
  ExpectContains(session, "UPDATE personas SET",
                 "La ayuda debe documentar UPDATE.");
  ExpectContains(session, "DELETE FROM personas",
                 "La ayuda debe documentar DELETE.");
  ExpectContains(session, "Sesion finalizada.",
                 "El comando exit debe cerrar la sesion.");
}

void TestPersistentInsertSession() {
  const std::string db_file = "test_cli_insert.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("personas", &table_heap, &hash_index);
    QueryProfiler profiler(&executor, &bpm, &disk_manager);

    std::istringstream input(
        "INSERT INTO personas VALUES "
        "(106, 'Ana Torres', 'Arequipa', 'Ingeniera');\n"
        "SELECT nombre FROM personas WHERE id = 106;\n"
        "UPDATE personas SET profesion = 'Arquitecta' "
        "WHERE id = 106;\n"
        "SELECT id, profesion FROM personas "
        "WHERE profesion = 'Arquitecta';\n"
        "INSERT INTO personas VALUES "
        "(107, 'Luis Mendoza', 'Lima', 'Medico');\n"
        "DELETE FROM personas WHERE id = 107;\n"
        "INSERT INTO personas VALUES "
        "(106, 'Duplicada', 'Lima', 'Medica');\n"
        "exit\n");
    std::ostringstream output;

    Expect(RunCli(input, output, &profiler) == 0,
           "La sesion con INSERT debe finalizar correctamente.");
    const std::string session = output.str();
    ExpectContains(session, "Persona insertada: id=106",
                   "La CLI debe confirmar el registro insertado.");
    ExpectContains(session, "Ana Torres",
                   "La CLI debe imprimir campos de texto.");
    ExpectContains(session, "Plan: Insert",
                   "La CLI debe informar el plan de insercion.");
    ExpectContains(session, "Plan: IndexScan",
                   "La fila insertada debe consultarse por el indice.");
    ExpectContains(session, "Filas actualizadas: 1",
                   "La CLI debe confirmar UPDATE.");
    ExpectContains(session, "Plan: Update",
                   "La CLI debe mostrar el plan UPDATE.");
    ExpectContains(session, "Filas eliminadas: 1",
                   "La CLI debe confirmar DELETE.");
    ExpectContains(session, "Plan: Delete",
                   "La CLI debe mostrar el plan DELETE.");
    ExpectContains(session, "ya existe",
                   "La CLI debe explicar el rechazo de un duplicado.");
    Expect(table_heap.GetTupleCount() == 1,
           "El duplicado no debe agregar otra fila fisica.");
    bpm.FlushAllPages();
  }

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    ExtensibleHashTable hash_index(&bpm);
    QueryExecutor executor("personas", &table_heap, &hash_index);

    const std::vector<Tuple> rows =
        executor.Execute("SELECT * FROM personas WHERE id = 106;");
    Expect(rows.size() == 1 &&
               rows[0].profesion == "Arquitecta",
           "INSERT y UPDATE de la CLI deben persistir al reiniciar.");
    Expect(executor.Execute(
               "SELECT * FROM personas WHERE id = 107;")
               .empty(),
           "DELETE de la CLI debe persistir al reiniciar.");
  }

  std::remove(db_file.c_str());
}

void TestEndOfInput() {
  const std::vector<Tuple> tuples;
  QueryExecutor executor("personas", tuples);
  QueryProfiler profiler(&executor);
  std::istringstream input;
  std::ostringstream output;

  Expect(RunCli(input, output, &profiler) == 0,
         "El fin de entrada debe cerrar la CLI correctamente.");
}

void TestUtf8BomOnFirstCommand() {
  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"}};
  QueryExecutor executor("personas", tuples);
  QueryProfiler profiler(&executor);
  std::istringstream input(
      "\xEF\xBB\xBF"
      "SELECT * FROM personas;\n"
      "exit\n");
  std::ostringstream output;

  Expect(RunCli(input, output, &profiler) == 0,
         "La CLI debe aceptar una entrada UTF-8 con BOM.");
  ExpectContains(output.str(), "Filas: 1",
                 "El BOM no debe invalidar la primera consulta.");
}

void TestNullExecutor() {
  std::istringstream input("exit\n");
  std::ostringstream output;

  try {
    RunCli(input, output, nullptr);
    Expect(false, "La CLI debe rechazar un ejecutor nulo.");
  } catch (const std::invalid_argument &) {
    // Resultado esperado.
  }
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE CLI ===\n";

  TestInteractiveSession();
  TestPersistentInsertSession();
  TestEndOfInput();
  TestUtf8BomOnFirstCommand();
  TestNullExecutor();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de la CLI pasaron correctamente.\n";
  return 0;
}
