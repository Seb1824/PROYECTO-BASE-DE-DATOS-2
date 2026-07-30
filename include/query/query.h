#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace minisgbd {

enum class ComparisonOperator {
  kEqual,
  kNotEqual,
  kLess,
  kLessOrEqual,
  kGreater,
  kGreaterOrEqual,
};

struct Condition {
  std::string column;
  ComparisonOperator comparison{ComparisonOperator::kEqual};
  int value{0};
};

struct SelectQuery {
  std::string table;
  bool select_all{true};
  std::vector<std::string> columns;
  std::optional<Condition> where;
};

struct InsertQuery {
  std::string table;
  int key{0};
  int value{0};
};

struct UpdateQuery {
  std::string table;
  std::string column;
  int value{0};
  std::optional<Condition> where;
};

struct DeleteQuery {
  std::string table;
  std::optional<Condition> where;
};

using QueryStatement =
    std::variant<SelectQuery, InsertQuery, UpdateQuery, DeleteQuery>;

}  // namespace minisgbd
