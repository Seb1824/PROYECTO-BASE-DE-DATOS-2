#pragma once

#include <optional>
#include <string>
#include <variant>

namespace minisgbd {

struct Condition {
  std::string column;
  int value{0};
};

struct SelectQuery {
  std::string table;
  bool select_all{true};
  std::optional<Condition> where;
};

struct InsertQuery {
  std::string table;
  int key{0};
  int value{0};
};

using QueryStatement = std::variant<SelectQuery, InsertQuery>;

}  // namespace minisgbd
