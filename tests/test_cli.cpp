#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "query/cli.h"
#include "query/query_executor.h"
#include "query/tuple.h"

using minisgbd::QueryExecutor;
using minisgbd::RunCli;
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
      Tuple{1, 100},
      Tuple{2, 200},
      Tuple{3, 200},
  };
  QueryExecutor executor("registros", tuples);

  std::istringstream input(
      "\n"
      "SELECT * FROM registros;\n"
      "SELECT * FROM registros WHERE value = 200;\n"
      "SELECT FROM registros;\n"
      "help\n"
      "exit\n");
  std::ostringstream output;

  const int result = RunCli(input, output, &executor);
  const std::string session = output.str();

  Expect(result == 0, "La CLI debe finalizar con codigo cero.");
  ExpectContains(session, "1           100",
                 "SELECT debe imprimir la primera fila.");
  ExpectContains(session, "Filas: 3",
                 "SELECT sin WHERE debe informar tres filas.");
  ExpectContains(session, "Plan: SeqScan",
                 "Debe mostrar el plan de escaneo secuencial.");
  ExpectContains(session, "Filas: 2",
                 "El filtro debe informar dos coincidencias.");
  ExpectContains(session, "Plan: Filter + SeqScan",
                 "Debe mostrar el plan de filtrado.");
  ExpectContains(session, "[ERROR]",
                 "Una consulta invalida debe mostrar un error.");
  ExpectContains(session, "Comandos disponibles:",
                 "El comando help debe mostrar ayuda.");
  ExpectContains(session, "Sesion finalizada.",
                 "El comando exit debe cerrar la sesion.");
}

void TestEndOfInput() {
  const std::vector<Tuple> tuples;
  QueryExecutor executor("registros", tuples);
  std::istringstream input;
  std::ostringstream output;

  Expect(RunCli(input, output, &executor) == 0,
         "El fin de entrada debe cerrar la CLI correctamente.");
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
  TestEndOfInput();
  TestNullExecutor();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de la CLI pasaron correctamente.\n";
  return 0;
}
