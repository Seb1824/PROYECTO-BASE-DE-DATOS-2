#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "query/parser.h"

using minisgbd::ComparisonOperator;
using minisgbd::DeleteQuery;
using minisgbd::InsertQuery;
using minisgbd::Parser;
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

void TestSelects() {
  const SelectQuery all = Parser::Parse("SELECT * FROM personas;");
  Expect(all.table == "personas" && all.select_all,
         "Debe reconocer SELECT * sobre personas.");
  Expect(!all.where.has_value(),
         "SELECT simple no debe crear un WHERE.");

  const SelectQuery filtered = Parser::Parse(
      "SELECT nombre, profesion FROM personas "
      "WHERE ciudad = 'Arequipa';");
  Expect(!filtered.select_all && filtered.columns.size() == 2,
         "Debe reconocer una proyeccion de texto.");
  Expect(filtered.columns[0] == "nombre" &&
             filtered.columns[1] == "profesion",
         "Debe conservar el orden de las columnas.");
  Expect(filtered.where.has_value() &&
             filtered.where->column == "ciudad",
         "Debe reconocer el filtro por ciudad.");
  if (filtered.where.has_value()) {
    Expect(std::holds_alternative<std::string>(
               filtered.where->value) &&
               std::get<std::string>(filtered.where->value) ==
                   "Arequipa",
           "Debe interpretar el literal de texto.");
  }

  const SelectQuery by_id = Parser::Parse(
      " select * from personas where id = -25 ");
  Expect(by_id.where.has_value() &&
             std::holds_alternative<int>(by_id.where->value) &&
             std::get<int>(by_id.where->value) == -25,
         "Debe aceptar ids enteros con signo.");
}

void TestComparisonsAndEscaping() {
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
        "SELECT * FROM personas WHERE id " + entry.first + " 10;");
    Expect(query.where.has_value() &&
               query.where->comparison == entry.second,
           "Debe reconocer el comparador " + entry.first + ".");
  }

  const SelectQuery apostrophe = Parser::Parse(
      "SELECT * FROM personas WHERE nombre = 'D''Angelo';");
  Expect(apostrophe.where.has_value() &&
             std::get<std::string>(apostrophe.where->value) ==
                 "D'Angelo",
         "Debe decodificar una comilla SQL duplicada.");
}

void TestInsert() {
  const QueryStatement statement = Parser::ParseStatement(
      "INSERT INTO personas VALUES "
      "(106, 'Ana Torres', 'Arequipa', 'Ingeniera de Software');");
  Expect(std::holds_alternative<InsertQuery>(statement),
         "Debe reconocer INSERT de una persona.");
  if (std::holds_alternative<InsertQuery>(statement)) {
    const InsertQuery &query = std::get<InsertQuery>(statement);
    Expect(query.table == "personas" && query.id == 106,
           "INSERT debe conservar tabla e id.");
    Expect(query.nombre == "Ana Torres" &&
               query.ciudad == "Arequipa" &&
               query.profesion == "Ingeniera de Software",
           "INSERT debe conservar los tres campos de texto.");
  }
}

void TestUpdateAndDelete() {
  const QueryStatement update_statement = Parser::ParseStatement(
      "UPDATE personas SET profesion = 'Arquitecta' "
      "WHERE id = 106;");
  Expect(std::holds_alternative<UpdateQuery>(update_statement),
         "Debe reconocer UPDATE.");
  if (std::holds_alternative<UpdateQuery>(update_statement)) {
    const UpdateQuery &query = std::get<UpdateQuery>(update_statement);
    Expect(query.column == "profesion" &&
               std::get<std::string>(query.value) == "Arquitecta",
           "UPDATE debe reconocer el nuevo texto.");
    Expect(query.where.has_value() &&
               std::get<int>(query.where->value) == 106,
           "UPDATE debe reconocer WHERE por id.");
  }

  const QueryStatement delete_statement = Parser::ParseStatement(
      "DELETE FROM personas WHERE ciudad <> 'Lima';");
  Expect(std::holds_alternative<DeleteQuery>(delete_statement),
         "Debe reconocer DELETE.");
  if (std::holds_alternative<DeleteQuery>(delete_statement)) {
    const DeleteQuery &query = std::get<DeleteQuery>(delete_statement);
    Expect(query.where.has_value() &&
               query.where->comparison ==
                   ComparisonOperator::kNotEqual &&
               std::get<std::string>(query.where->value) == "Lima",
           "DELETE debe conservar su condicion de texto.");
  }

  Expect(std::holds_alternative<UpdateQuery>(
             Parser::ParseStatement(
                 "UPDATE personas SET ciudad = 'Cusco';")),
         "UPDATE sin WHERE debe ser valido.");
  Expect(std::holds_alternative<DeleteQuery>(
             Parser::ParseStatement("DELETE FROM personas;")),
         "DELETE sin WHERE debe ser valido.");
}

void TestInvalidQueries() {
  const std::vector<std::string> invalid_queries = {
      "",
      "SELECT FROM personas;",
      "SELECT * personas;",
      "SELECT * FROM 123personas;",
      "SELECT * FROM personas WHERE id;",
      "SELECT * FROM personas; contenido_extra",
      "SELECT * FROM personas WHERE id = 999999999999999999999999;",
      "INSERT personas VALUES (1, 'A', 'B', 'C');",
      "INSERT INTO personas VALUES (1, 'A', 'B');",
      "INSERT INTO personas VALUES (1, A, 'B', 'C');",
      "INSERT INTO personas VALUES ('1', 'A', 'B', 'C');",
      "SELECT nombre, FROM personas;",
      "SELECT desconocida + 1 FROM personas;",
      "UPDATE personas profesion = 'A';",
      "UPDATE personas SET profesion = texto;",
      "DELETE personas WHERE id = 1;",
      "DELETE FROM personas WHERE id LIKE 1;",
      "SELECT * FROM personas WHERE ciudad = 'Lima;"};

  for (const std::string &sql : invalid_queries) {
    ExpectInvalid(sql);
  }
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DEL PARSER SQL ===\n";
  TestSelects();
  TestComparisonsAndEscaping();
  TestInsert();
  TestUpdateAndDelete();
  TestInvalidQueries();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }
  std::cout << "Todas las pruebas del parser pasaron correctamente.\n";
  return 0;
}
