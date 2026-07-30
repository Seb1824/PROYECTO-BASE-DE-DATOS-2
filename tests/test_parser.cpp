#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "query/parser.h"

using minisgbd::Parser;
using minisgbd::ComparisonOperator;
using minisgbd::DeleteQuery;
using minisgbd::InsertQuery;
using minisgbd::QueryStatement;
using minisgbd::SelectQuery;
using minisgbd::UpdateQuery;

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
    Parser::ParseStatement(sql);
    Expect(false, "La sentencia debio ser rechazada: " + sql);
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

void TestProjectionAndComparisons() {
  QueryStatement statement = Parser::ParseStatement(
      "SELECT value, id FROM registros WHERE value >= 500;");
  Expect(std::holds_alternative<SelectQuery>(statement),
         "Debe reconocer SELECT con proyeccion.");
  if (std::holds_alternative<SelectQuery>(statement)) {
    const SelectQuery &query = std::get<SelectQuery>(statement);
    Expect(!query.select_all && query.columns.size() == 2,
           "Debe conservar las columnas proyectadas.");
    Expect(query.columns.size() == 2 && query.columns[0] == "value" &&
               query.columns[1] == "id",
           "Debe conservar el orden de la proyeccion.");
    Expect(query.where.has_value() &&
               query.where->comparison ==
                   ComparisonOperator::kGreaterOrEqual,
           "Debe reconocer el comparador mayor o igual.");
  }

  const std::vector<std::pair<std::string, ComparisonOperator>>
      comparisons = {
          {"!=", ComparisonOperator::kNotEqual},
          {"<>", ComparisonOperator::kNotEqual},
          {"<", ComparisonOperator::kLess},
          {"<=", ComparisonOperator::kLessOrEqual},
          {">", ComparisonOperator::kGreater},
          {">=", ComparisonOperator::kGreaterOrEqual},
      };
  for (const auto &entry : comparisons) {
    const SelectQuery query = Parser::Parse(
        "SELECT * FROM registros WHERE key " + entry.first + " 10;");
    Expect(query.where.has_value() &&
               query.where->comparison == entry.second,
           "Debe reconocer el comparador " + entry.first + ".");
  }
}

void TestInsert() {
  QueryStatement statement =
      Parser::ParseStatement("INSERT INTO registros VALUES (106, 530);");

  Expect(std::holds_alternative<InsertQuery>(statement),
         "Debe reconocer una sentencia INSERT.");
  if (std::holds_alternative<InsertQuery>(statement)) {
    const InsertQuery &query = std::get<InsertQuery>(statement);
    Expect(query.table == "registros",
           "INSERT debe reconocer el nombre de la tabla.");
    Expect(query.key == 106, "INSERT debe reconocer la clave.");
    Expect(query.value == 530, "INSERT debe reconocer el valor.");
  }
}

void TestInsertCaseWhitespaceAndSignedValues() {
  QueryStatement statement = Parser::ParseStatement(
      "  insert into Inventario values ( -7 , +25 )  ");

  Expect(std::holds_alternative<InsertQuery>(statement),
         "Debe aceptar INSERT sin importar mayusculas y espacios.");
  if (std::holds_alternative<InsertQuery>(statement)) {
    const InsertQuery &query = std::get<InsertQuery>(statement);
    Expect(query.table == "Inventario",
           "INSERT debe conservar el nombre original de la tabla.");
    Expect(query.key == -7 && query.value == 25,
           "INSERT debe aceptar enteros con signo.");
  }
}

void TestUpdateAndDelete() {
  QueryStatement update_statement = Parser::ParseStatement(
      "UPDATE registros SET value = 535 WHERE id = 106;");
  Expect(std::holds_alternative<UpdateQuery>(update_statement),
         "Debe reconocer UPDATE.");
  if (std::holds_alternative<UpdateQuery>(update_statement)) {
    const UpdateQuery &query = std::get<UpdateQuery>(update_statement);
    Expect(query.table == "registros" && query.column == "value" &&
               query.value == 535,
           "UPDATE debe reconocer tabla, columna y valor.");
    Expect(query.where.has_value() && query.where->column == "id" &&
               query.where->value == 106,
           "UPDATE debe reconocer WHERE.");
  }

  QueryStatement delete_statement = Parser::ParseStatement(
      "DELETE FROM registros WHERE value < 500;");
  Expect(std::holds_alternative<DeleteQuery>(delete_statement),
         "Debe reconocer DELETE.");
  if (std::holds_alternative<DeleteQuery>(delete_statement)) {
    const DeleteQuery &query = std::get<DeleteQuery>(delete_statement);
    Expect(query.where.has_value() &&
               query.where->comparison == ComparisonOperator::kLess,
           "DELETE debe reconocer su condicion.");
  }

  Expect(std::holds_alternative<UpdateQuery>(
             Parser::ParseStatement(
                 "UPDATE registros SET value = 1;")),
         "UPDATE sin WHERE debe ser valido.");
  Expect(std::holds_alternative<DeleteQuery>(
             Parser::ParseStatement("DELETE FROM registros;")),
         "DELETE sin WHERE debe ser valido.");
}

void TestInvalidQueries() {
  const std::vector<std::string> invalid_queries = {
      "",
      "SELECT FROM registros;",
      "SELECT * registros;",
      "SELECT * FROM 123registros;",
      "SELECT * FROM registros WHERE id;",
      "SELECT * FROM registros WHERE id = texto;",
      "SELECT * FROM registros; contenido_extra",
      "SELECT * FROM registros WHERE id = 999999999999999999999999;",
      "INSERT registros VALUES (1, 10);",
      "INSERT INTO registros (1, 10);",
      "INSERT INTO registros VALUES 1, 10;",
      "INSERT INTO registros VALUES (1);",
      "INSERT INTO registros VALUES (1, texto);",
      "INSERT INTO registros VALUES (1, 10, 20);",
      "INSERT INTO registros VALUES (999999999999999999999, 10);",
      "SELECT key, FROM registros;",
      "SELECT desconocida + 1 FROM registros;",
      "UPDATE registros value = 10;",
      "UPDATE registros SET value = texto;",
      "DELETE registros WHERE id = 1;",
      "DELETE FROM registros WHERE id LIKE 1;"};

  for (const std::string &sql : invalid_queries) {
    ExpectInvalid(sql);
  }
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DEL PARSER SQL ===\n";

  TestSelectWithoutWhere();
  TestSelectWithWhere();
  TestCaseWhitespaceAndSignedValue();
  TestProjectionAndComparisons();
  TestInsert();
  TestInsertCaseWhitespaceAndSignedValues();
  TestUpdateAndDelete();
  TestInvalidQueries();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas del parser pasaron correctamente.\n";
  return 0;
}
