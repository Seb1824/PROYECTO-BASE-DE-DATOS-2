#pragma once

#include <string>

#include "query/query.h"

namespace minisgbd {

class Parser {
 public:
  static SelectQuery Parse(const std::string &sql);
  static QueryStatement ParseStatement(const std::string &sql);
};

}  // namespace minisgbd
