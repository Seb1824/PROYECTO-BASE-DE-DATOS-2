#include "query/parser.h"

#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

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
      R"(^\s*SELECT\s+(\*|[A-Za-z_][A-Za-z0-9_]*(?:\s*,\s*[A-Za-z_][A-Za-z0-9_]*)*)\s+FROM\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+WHERE\s+([A-Za-z_][A-Za-z0-9_]*)\s*(=|!=|<>|<=|>=|<|>)\s*([+-]?[0-9]+))?\s*;?\s*$)",
      std::regex_constants::icase);
  return pattern;
}

const std::regex &InsertPattern() {
  static const std::regex pattern(
      R"(^\s*INSERT\s+INTO\s+([A-Za-z_][A-Za-z0-9_]*)\s+VALUES\s*\(\s*([+-]?[0-9]+)\s*,\s*([+-]?[0-9]+)\s*\)\s*;?\s*$)",
      std::regex_constants::icase);
  return pattern;
}

const std::regex &UpdatePattern() {
  static const std::regex pattern(
      R"(^\s*UPDATE\s+([A-Za-z_][A-Za-z0-9_]*)\s+SET\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([+-]?[0-9]+)(?:\s+WHERE\s+([A-Za-z_][A-Za-z0-9_]*)\s*(=|!=|<>|<=|>=|<|>)\s*([+-]?[0-9]+))?\s*;?\s*$)",
      std::regex_constants::icase);
  return pattern;
}

const std::regex &DeletePattern() {
  static const std::regex pattern(
      R"(^\s*DELETE\s+FROM\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+WHERE\s+([A-Za-z_][A-Za-z0-9_]*)\s*(=|!=|<>|<=|>=|<|>)\s*([+-]?[0-9]+))?\s*;?\s*$)",
      std::regex_constants::icase);
  return pattern;
}

ComparisonOperator ParseComparison(const std::string &text) {
  if (text == "=") {
    return ComparisonOperator::kEqual;
  }
  if (text == "!=" || text == "<>") {
    return ComparisonOperator::kNotEqual;
  }
  if (text == "<") {
    return ComparisonOperator::kLess;
  }
  if (text == "<=") {
    return ComparisonOperator::kLessOrEqual;
  }
  if (text == ">") {
    return ComparisonOperator::kGreater;
  }
  if (text == ">=") {
    return ComparisonOperator::kGreaterOrEqual;
  }
  throw std::invalid_argument("Operador de comparacion desconocido.");
}

Condition ParseCondition(const std::smatch &match, std::size_t column_index,
                         std::size_t comparison_index,
                         std::size_t value_index,
                         const std::string &context) {
  Condition condition;
  condition.column = match[column_index].str();
  condition.comparison =
      ParseComparison(match[comparison_index].str());
  condition.value =
      ParseInteger(match[value_index].str(), context);
  return condition;
}

std::vector<std::string> ParseColumns(const std::string &text) {
  std::vector<std::string> columns;
  static const std::regex separator(R"(\s*,\s*)");
  std::sregex_token_iterator iterator(
      text.begin(), text.end(), separator, -1);
  const std::sregex_token_iterator end;
  for (; iterator != end; ++iterator) {
    columns.push_back(iterator->str());
  }
  return columns;
}

SelectQuery ParseSelectMatch(const std::smatch &match) {
  SelectQuery query;
  query.select_all = match[1].str() == "*";
  if (!query.select_all) {
    query.columns = ParseColumns(match[1].str());
  }
  query.table = match[2].str();

  if (match[3].matched) {
    query.where =
        ParseCondition(match, 3, 4, 5, "El valor de WHERE");
  }

  return query;
}

}  // namespace

SelectQuery Parser::Parse(const std::string &sql) {
  QueryStatement statement = ParseStatement(sql);
  if (!std::holds_alternative<SelectQuery>(statement)) {
    throw std::invalid_argument(
        "Se esperaba una consulta SELECT.");
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

  if (std::regex_match(sql, match, UpdatePattern())) {
    UpdateQuery query;
    query.table = match[1].str();
    query.column = match[2].str();
    query.value =
        ParseInteger(match[3].str(), "El valor de SET");
    if (match[4].matched) {
      query.where =
          ParseCondition(match, 4, 5, 6, "El valor de WHERE");
    }
    return query;
  }

  if (std::regex_match(sql, match, DeletePattern())) {
    DeleteQuery query;
    query.table = match[1].str();
    if (match[2].matched) {
      query.where =
          ParseCondition(match, 2, 3, 4, "El valor de WHERE");
    }
    return query;
  }

  throw std::invalid_argument(
      "Sentencia invalida. Formatos esperados: "
      "SELECT columnas FROM tabla [WHERE condicion]; "
      "INSERT INTO tabla VALUES (clave, valor); "
      "UPDATE tabla SET columna = valor [WHERE condicion]; o "
      "DELETE FROM tabla [WHERE condicion];");
}

}  // namespace minisgbd
