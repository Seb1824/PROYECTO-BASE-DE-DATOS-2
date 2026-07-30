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

  std::string trimmed(first, last.base());
  if (trimmed.size() >= 3 &&
      static_cast<unsigned char>(trimmed[0]) == 0xEF &&
      static_cast<unsigned char>(trimmed[1]) == 0xBB &&
      static_cast<unsigned char>(trimmed[2]) == 0xBF) {
    trimmed.erase(0, 3);
  }
  return trimmed;
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
    case QueryPlanType::kInsert:
      return "Insert";
    case QueryPlanType::kUpdate:
      return "Update";
    case QueryPlanType::kDelete:
      return "Delete";
  }

  return "Desconocido";
}

void PrintHelp(std::ostream &output) {
  output << "Comandos disponibles:\n"
         << "  SELECT * FROM registros;\n"
         << "  SELECT * FROM registros WHERE id = 101;\n"
         << "  SELECT key FROM registros WHERE value >= 505;\n"
         << "  SELECT * FROM registros WHERE value = 505;\n"
         << "  INSERT INTO registros VALUES (106, 530);\n"
         << "  UPDATE registros SET value = 535 WHERE id = 106;\n"
         << "  DELETE FROM registros WHERE id = 106;\n"
         << "  help  Muestra esta ayuda.\n"
         << "  exit  Cierra la CLI.\n";
}

void PrintResults(std::ostream &output, const ProfiledQueryResult &result) {
  if (result.plan_type == QueryPlanType::kInsert) {
    if (!result.rows.empty()) {
      const Tuple &tuple = result.rows.front();
      output << "Registro insertado: key=" << tuple.key
             << ", value=" << tuple.value << '\n';
    }
    output << "Filas afectadas: " << result.rows.size() << '\n';
  } else if (result.plan_type == QueryPlanType::kUpdate) {
    output << "Filas actualizadas: " << result.rows.size() << '\n';
  } else if (result.plan_type == QueryPlanType::kDelete) {
    output << "Filas eliminadas: " << result.rows.size() << '\n';
  } else if (result.rows.empty()) {
    output << "Sin resultados.\n";
    output << "Filas: 0\n";
  } else {
    for (const std::string &column : result.output_columns) {
      output << std::left << std::setw(12) << column;
    }
    output << '\n';
    output << std::string(result.output_columns.size() * 12, '-') << '\n';

    for (const Tuple &tuple : result.rows) {
      for (const std::string &column : result.output_columns) {
        if (column == "key") {
          output << std::left << std::setw(12) << tuple.key;
        } else {
          output << std::left << std::setw(12) << tuple.value;
        }
      }
      output << '\n';
    }

    output << "Filas: " << result.rows.size() << '\n';
  }
  output << "Plan: " << PlanName(result.plan_type) << '\n';
  output << std::fixed << std::setprecision(3);
  output << "Tiempo: " << result.metrics.elapsed_ms << " ms\n";
  output << "Buffer hits: " << result.metrics.buffer_hits << '\n';
  output << "Buffer misses: " << result.metrics.buffer_misses << '\n';
  output << std::setprecision(2);
  output << "Hit ratio: " << result.metrics.buffer_hit_ratio * 100.0 << " %\n";
  output << "Lecturas de disco: " << result.metrics.disk_reads << '\n';
  output << "Escrituras de disco: " << result.metrics.disk_writes << '\n';
  output << "Costo I/O: " << result.metrics.io_operations
         << " operaciones de pagina\n";
  output << std::defaultfloat;
}

}  // namespace

int RunCli(std::istream &input, std::ostream &output,
           QueryProfiler *profiler) {
  if (profiler == nullptr) {
    throw std::invalid_argument(
        "RunCli requiere un QueryProfiler valido.");
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
      const ProfiledQueryResult result = profiler->Execute(command);
      PrintResults(output, result);
    } catch (const std::exception &error) {
      output << "[ERROR] " << error.what() << '\n';
    }
  }

  return 0;
}

}  // namespace minisgbd
