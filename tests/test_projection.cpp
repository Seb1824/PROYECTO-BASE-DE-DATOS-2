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
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Cusco", "Medico"},
  };
  SeqScanOperator scan(tuples);
  ProjectionOperator identity_projection(
      &scan, {"id", "nombre"});
  Tuple tuple;

  Expect(!identity_projection.Next(&tuple),
         "Next antes de Open debe retornar false.");
  identity_projection.Open();
  Expect(identity_projection.Next(&tuple) && tuple.id == 1 &&
             tuple.nombre == "Ana" && tuple.ciudad.empty() &&
             tuple.profesion.empty(),
         "La proyeccion debe ocultar ciudad y profesion.");
  Expect(identity_projection.Next(&tuple) && tuple.id == 2 &&
             tuple.nombre == "Bruno",
         "La proyeccion debe conservar todas las filas.");
  Expect(!identity_projection.Next(&tuple),
         "Next debe terminar al consumir el operador hijo.");
  identity_projection.Close();
  Expect(!identity_projection.Next(&tuple),
         "Next despues de Close debe retornar false.");
}

void TestProfessionProjection() {
  const std::vector<Tuple> tuples = {
      Tuple{7, "Sofia", "Trujillo", "Abogada"}};
  SeqScanOperator scan(tuples);
  ProjectionOperator profession_projection(&scan, {"profesion"});
  profession_projection.Open();

  Tuple tuple;
  Expect(profession_projection.Next(&tuple) && tuple.id == 0 &&
             tuple.nombre.empty() && tuple.ciudad.empty() &&
             tuple.profesion == "Abogada",
         "La proyeccion de profesion debe ocultar las otras columnas.");
  profession_projection.Close();
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE PROJECTION OPERATOR ===\n";
  TestProjectionLifecycle();
  TestProfessionProjection();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }
  std::cout << "Todas las pruebas de ProjectionOperator pasaron.\n";
  return 0;
}
