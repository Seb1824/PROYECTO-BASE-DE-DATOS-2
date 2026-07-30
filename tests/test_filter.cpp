#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "query/filter_operator.h"
#include "query/seq_scan_operator.h"

using minisgbd::FilterOperator;
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

void TestSingleMatch() {
  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Cusco", "Medico"},
      Tuple{3, "Carla", "Piura", "Abogada"},
  };

  SeqScanOperator scan(tuples);
  FilterOperator filter(&scan,
                        [](const Tuple &tuple) { return tuple.id == 2; });
  Tuple output;

  filter.Open();
  Expect(filter.Next(&output), "El filtro debe encontrar el id 2.");
  Expect(output == tuples[1],
         "El filtro debe devolver el Tuple completo.");
  Expect(!filter.Next(&output),
         "No debe producir resultados adicionales para una coincidencia.");
  filter.Close();
}

void TestMultipleMatches() {
  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Arequipa", "Medico"},
      Tuple{3, "Carla", "Arequipa", "Abogada"},
      Tuple{4, "Diego", "Cusco", "Arquitecto"},
  };

  SeqScanOperator scan(tuples);
  FilterOperator filter(
      &scan, [](const Tuple &tuple) {
        return tuple.ciudad == "Arequipa";
      });
  Tuple output;
  const std::vector<int> expected_ids = {2, 3};
  std::size_t result_index = 0;

  filter.Open();
  while (filter.Next(&output)) {
    Expect(result_index < expected_ids.size(),
           "El filtro produjo mas resultados de los esperados.");
    if (result_index < expected_ids.size()) {
      Expect(output.id == expected_ids[result_index],
             "El filtro debe conservar el orden del operador hijo.");
    }
    ++result_index;
  }
  filter.Close();

  Expect(result_index == expected_ids.size(),
         "El filtro debe producir todas las coincidencias.");
}

void TestNoMatches() {
  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Cusco", "Medico"},
  };

  SeqScanOperator scan(tuples);
  FilterOperator filter(
      &scan, [](const Tuple &tuple) { return tuple.id == 999; });
  Tuple output;

  filter.Open();
  Expect(!filter.Next(&output),
         "Un predicado sin coincidencias debe retornar false.");
  filter.Close();
}

void TestLifecycleAndReopen() {
  const std::vector<Tuple> tuples = {
      Tuple{10, "Ana", "Lima", "Ingeniera"},
      Tuple{20, "Luis", "Cusco", "Medico"},
  };

  SeqScanOperator scan(tuples);
  FilterOperator filter(
      &scan, [](const Tuple &tuple) {
        return !tuple.profesion.empty();
      });
  Tuple output;

  Expect(!filter.Next(&output),
         "Next() antes de Open() debe retornar false.");

  filter.Open();
  Expect(!filter.Next(nullptr), "Next(nullptr) debe retornar false.");
  Expect(filter.Next(&output),
         "Un puntero nulo no debe consumir el siguiente resultado.");
  Expect(output.id == 10, "La primera ejecucion debe iniciar en el id 10.");
  filter.Close();

  Expect(!filter.Next(&output),
         "Next() despues de Close() debe retornar false.");

  filter.Open();
  Expect(filter.Next(&output), "Open() debe permitir volver a ejecutar.");
  Expect(output.id == 10, "Open() debe reiniciar toda la cadena Volcano.");
  filter.Close();
}

void TestInvalidConfiguration() {
  try {
    FilterOperator filter(
        nullptr, [](const Tuple &tuple) { return tuple.id == 1; });
    Expect(false, "Debe rechazar un operador hijo nulo.");
  } catch (const std::invalid_argument &) {
    // Resultado esperado.
  }

  const std::vector<Tuple> tuples;
  SeqScanOperator scan(tuples);

  try {
    FilterOperator filter(&scan, std::function<bool(const Tuple &)>{});
    Expect(false, "Debe rechazar un predicado vacio.");
  } catch (const std::invalid_argument &) {
    // Resultado esperado.
  }
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE FILTER OPERATOR ===\n";

  TestSingleMatch();
  TestMultipleMatches();
  TestNoMatches();
  TestLifecycleAndReopen();
  TestInvalidConfiguration();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de FilterOperator pasaron correctamente.\n";
  return 0;
}
