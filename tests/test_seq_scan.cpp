#include <iostream>
#include <string>
#include <vector>

#include "query/seq_scan_operator.h"

using minisgbd::SeqScanOperator;
using minisgbd::Tuple;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

void TestScanLifecycle() {
  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Cusco", "Medico"},
      Tuple{3, "Carla", "Piura", "Abogada"},
  };

  SeqScanOperator scan(tuples);
  Tuple output;

  Expect(!scan.Next(&output), "Next() antes de Open() debe retornar false.");

  scan.Open();

  for (std::size_t index = 0; index < tuples.size(); ++index) {
    Expect(scan.Next(&output), "Debe producir todos los registros.");
    Expect(output == tuples[index],
           "Debe conservar cada persona y su orden.");
  }

  Expect(!scan.Next(&output),
         "Next() debe retornar false al terminar los registros.");

  scan.Close();
  Expect(!scan.Next(&output),
         "Next() despues de Close() debe retornar false.");
}

void TestEmptyScan() {
  const std::vector<Tuple> tuples;
  SeqScanOperator scan(tuples);
  Tuple output;

  scan.Open();
  Expect(!scan.Next(&output),
         "Una coleccion vacia no debe producir resultados.");
  scan.Close();
}

void TestReopenRestartsScan() {
  const std::vector<Tuple> tuples = {
      Tuple{10, "Ana", "Lima", "Ingeniera"},
      Tuple{20, "Luis", "Cusco", "Medico"},
  };

  SeqScanOperator scan(tuples);
  Tuple output;

  scan.Open();
  Expect(scan.Next(&output), "La primera ejecucion debe producir datos.");
  Expect(output.id == 10, "La primera ejecucion debe iniciar en el id 10.");
  scan.Close();

  scan.Open();
  Expect(scan.Next(&output), "Open() debe permitir volver a ejecutar el scan.");
  Expect(output.id == 10, "Open() debe reiniciar el cursor.");
  scan.Close();
}

void TestNullOutput() {
  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"}};
  SeqScanOperator scan(tuples);

  scan.Open();
  Expect(!scan.Next(nullptr), "Next(nullptr) debe retornar false.");

  Tuple output;
  Expect(scan.Next(&output),
         "Un puntero nulo no debe consumir el siguiente registro.");
  scan.Close();
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE SEQ SCAN OPERATOR ===\n";

  TestScanLifecycle();
  TestEmptyScan();
  TestReopenRestartsScan();
  TestNullOutput();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de SeqScanOperator pasaron correctamente.\n";
  return 0;
}
