#pragma once

#include <iosfwd>

#include "query/query_profiler.h"

namespace minisgbd {

int RunCli(std::istream &input, std::ostream &output,
           QueryProfiler *profiler);

}  // namespace minisgbd
