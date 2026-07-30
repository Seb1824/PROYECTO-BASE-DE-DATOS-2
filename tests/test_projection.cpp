#include <iostream>
#include <string>
#include <vector>

#include "query/projection_operator.h"
#include "query/seq_scan_operator.h"
#include "query/tuple.h"

using minisgbd::ProjectionOperator;
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

void TestProjectionLifecycle() {
  const std::vector<Tuple> tuples = {
      Tuple{1, 100},
      Tuple{2, 200},
  };
  SeqScanOperator scan(tuples);
  ProjectionOperator key_projection(&scan, true, false);
  Tuple tuple;

  Expect(!key_projection.Next(&tuple),
         "Next antes de Open debe retornar false.");
  key_projection.Open();
  Expect(key_projection.Next(&tuple) && tuple.key == 1 &&
             tuple.value == 0,
         "La proyeccion de key debe ocultar value.");
  Expect(key_projection.Next(&tuple) && tuple.key == 2 &&
             tuple.value == 0,
         "La proyeccion debe conservar todas las filas.");
  Expect(!key_projection.Next(&tuple),
         "Next debe terminar al consumir el operador hijo.");
  key_projection.Close();
  Expect(!key_projection.Next(&tuple),
         "Next despues de Close debe retornar false.");
}

void TestValueProjection() {
  const std::vector<Tuple> tuples = {Tuple{7, 700}};
  SeqScanOperator scan(tuples);
  ProjectionOperator value_projection(&scan, false, true);
  value_projection.Open();

  Tuple tuple;
  Expect(value_projection.Next(&tuple) && tuple.key == 0 &&
             tuple.value == 700,
         "La proyeccion de value debe ocultar key.");
  value_projection.Close();
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE PROJECTION OPERATOR ===\n";
  TestProjectionLifecycle();
  TestValueProjection();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }
  std::cout << "Todas las pruebas de ProjectionOperator pasaron.\n";
  return 0;
}
