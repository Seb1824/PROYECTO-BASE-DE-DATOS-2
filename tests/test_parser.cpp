#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "query/parser.h"

using minisgbd::Parser;
using minisgbd::SelectQuery;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

void ExpectInvalid(const std::string &sql) {
  try {
    Parser::Parse(sql);
    Expect(false, "La consulta debio ser rechazada: " + sql);
  } catch (const std::invalid_argument &) {
    // Resultado esperado.
  } catch (const std::exception &error) {
    Expect(false, "Tipo de error inesperado para '" + sql +
                      "': " + error.what());
  }
}

void TestSelectWithoutWhere() {
  SelectQuery query = Parser::Parse("SELECT * FROM registros;");

  Expect(query.table == "registros", "Debe reconocer el nombre de la tabla.");
  Expect(query.select_all, "SELECT * debe seleccionar todas las columnas.");
  Expect(!query.where.has_value(), "La consulta no debe tener condicion WHERE.");
}

void TestSelectWithWhere() {
  SelectQuery query =
      Parser::Parse("SELECT * FROM registros WHERE id = 25;");

  Expect(query.table == "registros", "Debe reconocer la tabla con WHERE.");
  Expect(query.where.has_value(), "Debe reconocer la condicion WHERE.");

  if (query.where.has_value()) {
    Expect(query.where->column == "id", "Debe reconocer la columna de WHERE.");
    Expect(query.where->value == 25, "Debe reconocer el valor entero de WHERE.");
  }
}

void TestCaseWhitespaceAndSignedValue() {
  SelectQuery query =
      Parser::Parse("  select  *  from Inventario where stock = -10  ");

  Expect(query.table == "Inventario",
         "Debe conservar el nombre original de la tabla.");
  Expect(query.where.has_value(), "Debe aceptar palabras clave en minusculas.");

  if (query.where.has_value()) {
    Expect(query.where->column == "stock",
           "Debe reconocer la columna con espacios adicionales.");
    Expect(query.where->value == -10, "Debe aceptar enteros con signo.");
  }
}

void TestInvalidQueries() {
  const std::vector<std::string> invalid_queries = {
      "",
      "SELECT FROM registros;",
      "SELECT id FROM registros;",
      "SELECT * registros;",
      "SELECT * FROM 123registros;",
      "SELECT * FROM registros WHERE id;",
      "SELECT * FROM registros WHERE id = texto;",
      "SELECT * FROM registros WHERE id > 10;",
      "SELECT * FROM registros; contenido_extra",
      "SELECT * FROM registros WHERE id = 999999999999999999999999;"};

  for (const std::string &sql : invalid_queries) {
    ExpectInvalid(sql);
  }
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DEL PARSER SELECT/WHERE ===\n";

  TestSelectWithoutWhere();
  TestSelectWithWhere();
  TestCaseWhitespaceAndSignedValue();
  TestInvalidQueries();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas del parser pasaron correctamente.\n";
  return 0;
}
