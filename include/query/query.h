#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace minisgbd {

using ScalarValue = std::variant<int, std::string>;

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
  ScalarValue value{0};
};

struct SelectQuery {
  std::string table;
  bool select_all{true};
  std::vector<std::string> columns;
  std::optional<Condition> where;
};

struct InsertQuery {
  std::string table;
  int id{0};
  std::string nombre;
  std::string ciudad;
  std::string profesion;
};

struct UpdateQuery {
  std::string table;
  std::string column;
  ScalarValue value{0};
  std::optional<Condition> where;
};

struct DeleteQuery {
  std::string table;
  std::optional<Condition> where;
};

using QueryStatement =
    std::variant<SelectQuery, InsertQuery, UpdateQuery, DeleteQuery>;

}  // namespace minisgbd
