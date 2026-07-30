#include "query/cli.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iomanip>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace minisgbd {
namespace {

std::string Trim(const std::string &text) {
  const auto first = std::find_if_not(
      text.begin(), text.end(),
      [](unsigned char character) { return std::isspace(character); });

  if (first == text.end()) {
    return "";
  }

  const auto last = std::find_if_not(
      text.rbegin(), text.rend(),
      [](unsigned char character) { return std::isspace(character); });

  return std::string(first, last.base());
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return text;
}

const char *PlanName(QueryPlanType plan_type) {
  switch (plan_type) {
    case QueryPlanType::kSeqScan:
      return "SeqScan";
    case QueryPlanType::kFilteredSeqScan:
      return "Filter + SeqScan";
    case QueryPlanType::kIndexScan:
      return "IndexScan";
  }

  return "Desconocido";
}

void PrintHelp(std::ostream &output) {
  output << "Comandos disponibles:\n"
         << "  SELECT * FROM registros;\n"
         << "  SELECT * FROM registros WHERE id = 101;\n"
         << "  SELECT * FROM registros WHERE value = 505;\n"
         << "  help  Muestra esta ayuda.\n"
         << "  exit  Cierra la CLI.\n";
}

void PrintResults(std::ostream &output, const std::vector<Tuple> &results,
                  QueryPlanType plan_type) {
  if (results.empty()) {
    output << "Sin resultados.\n";
  } else {
    output << std::left << std::setw(12) << "key"
           << "value\n";
    output << "--------------------\n";

    for (const Tuple &tuple : results) {
      output << std::left << std::setw(12) << tuple.key << tuple.value << '\n';
    }
  }

  output << "Filas: " << results.size() << '\n';
  output << "Plan: " << PlanName(plan_type) << '\n';
}

}  // namespace

int RunCli(std::istream &input, std::ostream &output,
           QueryExecutor *executor) {
  if (executor == nullptr) {
    throw std::invalid_argument(
        "RunCli requiere un QueryExecutor valido.");
  }

  output << "Escribe una consulta SQL, 'help' o 'exit'.\n";

  std::string line;
  while (true) {
    output << "mini-sgbd> " << std::flush;

    if (!std::getline(input, line)) {
      output << '\n';
      break;
    }

    const std::string command = Trim(line);
    if (command.empty()) {
      continue;
    }

    const std::string normalized_command = ToLower(command);
    if (normalized_command == "exit" || normalized_command == "quit") {
      output << "Sesion finalizada.\n";
      break;
    }

    if (normalized_command == "help") {
      PrintHelp(output);
      continue;
    }

    try {
      const std::vector<Tuple> results = executor->Execute(command);
      PrintResults(output, results, executor->GetLastPlanType());
    } catch (const std::exception &error) {
      output << "[ERROR] " << error.what() << '\n';
    }
  }

  return 0;
}

}  // namespace minisgbd
