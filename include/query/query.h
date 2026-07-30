#pragma once

#include <optional>
#include <string>

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

}  // namespace minisgbd
