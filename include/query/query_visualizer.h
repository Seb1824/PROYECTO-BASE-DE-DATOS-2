#pragma once

#include <string>

namespace minisgbd {

struct ProfiledQueryResult;

class QueryVisualizer {
 public:
  static void WriteHtml(const ProfiledQueryResult &result,
                        const std::string &path);
};

}  // namespace minisgbd
