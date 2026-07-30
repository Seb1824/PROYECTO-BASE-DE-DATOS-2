#include "query/parser.h"

#include <regex>
#include <stdexcept>
#include <string>

namespace minisgbd {
namespace {

int ParseInteger(const std::string &text, const std::string &context) {
  try {
    std::size_t parsed_characters = 0;
    const int value = std::stoi(text, &parsed_characters);
    if (parsed_characters != text.size()) {
      throw std::invalid_argument("valor incompleto");
    }
    return value;
  } catch (const std::exception &) {
    throw std::invalid_argument(context + " debe ser un entero valido.");
  }
}

const std::regex &SelectPattern() {
  static const std::regex pattern(
      R"(^\s*SELECT\s+\*\s+FROM\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+WHERE\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([+-]?[0-9]+))?\s*;?\s*$)",
      std::regex_constants::icase);
  return pattern;
}

const std::regex &InsertPattern() {
  static const std::regex pattern(
      R"(^\s*INSERT\s+INTO\s+([A-Za-z_][A-Za-z0-9_]*)\s+VALUES\s*\(\s*([+-]?[0-9]+)\s*,\s*([+-]?[0-9]+)\s*\)\s*;?\s*$)",
      std::regex_constants::icase);
  return pattern;
}

SelectQuery ParseSelectMatch(const std::smatch &match) {
  SelectQuery query;
  query.table = match[1].str();

  if (match[2].matched) {
    Condition condition;
    condition.column = match[2].str();
    condition.value = ParseInteger(match[3].str(), "El valor de WHERE");
    query.where = condition;
  }

  return query;
}

}  // namespace

SelectQuery Parser::Parse(const std::string &sql) {
  QueryStatement statement = ParseStatement(sql);
  if (!std::holds_alternative<SelectQuery>(statement)) {
    throw std::invalid_argument(
        "Se esperaba una consulta SELECT, pero se recibio INSERT.");
  }
  return std::get<SelectQuery>(statement);
}

QueryStatement Parser::ParseStatement(const std::string &sql) {
  std::smatch match;
  if (std::regex_match(sql, match, SelectPattern())) {
    return ParseSelectMatch(match);
  }

  if (std::regex_match(sql, match, InsertPattern())) {
    InsertQuery query;
    query.table = match[1].str();
    query.key = ParseInteger(match[2].str(), "La clave de INSERT");
    query.value = ParseInteger(match[3].str(), "El valor de INSERT");
    return query;
  }

  throw std::invalid_argument(
      "Sentencia invalida. Formatos esperados: "
      "SELECT * FROM tabla [WHERE columna = entero]; o "
      "INSERT INTO tabla VALUES (clave, valor);");
}

}  // namespace minisgbd
