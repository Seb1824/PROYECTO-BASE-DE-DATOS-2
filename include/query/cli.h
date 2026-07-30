#pragma once

#include <iosfwd>

#include "query/query_executor.h"

namespace minisgbd {

int RunCli(std::istream &input, std::ostream &output,
           QueryExecutor *executor);

}  // namespace minisgbd
