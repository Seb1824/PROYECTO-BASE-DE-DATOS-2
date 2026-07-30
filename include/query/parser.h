#pragma once

#include <string>

#include "query/query.h"

namespace minisgbd {

class Parser {
 public:
  static SelectQuery Parse(const std::string &sql);
};

}  // namespace minisgbd
