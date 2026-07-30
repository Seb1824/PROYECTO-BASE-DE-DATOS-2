#include "query/parser.h"

#include <regex>
#include <stdexcept>
#include <string>

namespace minisgbd {

SelectQuery Parser::Parse(const std::string &sql) {
  static const std::regex select_pattern(
      R"(^\s*SELECT\s+\*\s+FROM\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+WHERE\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([+-]?[0-9]+))?\s*;?\s*$)",
      std::regex_constants::icase);

  std::smatch match;
  if (!std::regex_match(sql, match, select_pattern)) {
    throw std::invalid_argument(
        "Consulta invalida. Formato esperado: "
        "SELECT * FROM tabla [WHERE columna = entero];");
  }

  SelectQuery query;
  query.table = match[1].str();

  if (match[2].matched) {
    Condition condition;
    condition.column = match[2].str();

    try {
      std::size_t parsed_characters = 0;
      condition.value = std::stoi(match[3].str(), &parsed_characters);
      if (parsed_characters != match[3].str().size()) {
        throw std::invalid_argument("valor incompleto");
      }
    } catch (const std::exception &) {
      throw std::invalid_argument(
          "El valor de WHERE debe ser un entero valido.");
    }

    query.where = condition;
  }

  return query;
}

}  // namespace minisgbd
