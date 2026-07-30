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

#include "query/car_visualizer_demo.h"

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
    case QueryPlanType::kCarDemo:
      return "CAR Demo";
  }

  return "Desconocido";
}

void PrintHelp(std::ostream &output) {
  output << "Comandos disponibles:\n"
         << "  SELECT * FROM personas;\n"
         << "  SELECT * FROM personas WHERE id = 101;\n"
         << "  SELECT nombre, profesion FROM personas "
            "WHERE ciudad = 'Arequipa';\n"
         << "  INSERT INTO personas VALUES "
            "(106, 'Ana Torres', 'Arequipa', 'Ingeniera');\n"
         << "  UPDATE personas SET profesion = 'Arquitecta' "
            "WHERE id = 106;\n"
         << "  DELETE FROM personas WHERE id = 106;\n"
         << "  car-demo  Genera una traza adaptativa completa de CAR.\n"
         << "  visualize  Muestra la ruta del perfil visual generado.\n"
         << "  help  Muestra esta ayuda.\n"
         << "  exit  Cierra la CLI.\n";
}

std::string ColumnValue(const Tuple &tuple,
                        const std::string &column) {
  if (column == "id") {
    return std::to_string(tuple.id);
  }
  if (column == "nombre") {
    return tuple.nombre;
  }
  if (column == "ciudad") {
    return tuple.ciudad;
  }
  return tuple.profesion;
}

void PrintResults(std::ostream &output, const ProfiledQueryResult &result) {
  if (result.plan_type == QueryPlanType::kInsert) {
    if (!result.rows.empty()) {
      const Tuple &tuple = result.rows.front();
      output << "Persona insertada: id=" << tuple.id
             << ", nombre='" << tuple.nombre
             << "', ciudad='" << tuple.ciudad
             << "', profesion='" << tuple.profesion << "'\n";
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
    std::vector<std::size_t> widths;
    widths.reserve(result.output_columns.size());
    for (const std::string &column : result.output_columns) {
      std::size_t width = column.size();
      for (const Tuple &tuple : result.rows) {
        width = std::max(
            width, ColumnValue(tuple, column).size());
      }
      widths.push_back(width + 2);
    }

    for (std::size_t index = 0;
         index < result.output_columns.size(); ++index) {
      output << std::left
             << std::setw(static_cast<int>(widths[index]))
             << result.output_columns[index];
    }
    output << '\n';
    for (const std::size_t width : widths) {
      output << std::string(width - 2, '-') << "  ";
    }
    output << '\n';

    for (const Tuple &tuple : result.rows) {
      for (std::size_t index = 0;
           index < result.output_columns.size(); ++index) {
        output << std::left
               << std::setw(static_cast<int>(widths[index]))
               << ColumnValue(tuple, result.output_columns[index]);
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
  if (!result.visualization_path.empty()) {
    output << "Visualizacion: " << result.visualization_path << '\n';
  }
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

    if (normalized_command == "visualize" ||
        normalized_command == "visualizar") {
      if (profiler->GetVisualizationPath().empty()) {
        output << "La visualizacion automatica no esta configurada.\n";
      } else {
        output << "Perfil visual: "
               << profiler->GetVisualizationPath()
               << "\nEjecuta primero una consulta para actualizarlo.\n";
      }
      continue;
    }

    if (normalized_command == "car-demo") {
      const std::string demo_path = "build/car_demo.html";
      try {
        GenerateCARDemoReport(demo_path);
        output << "Demostracion CAR generada: " << demo_path << '\n';
      } catch (const std::exception &error) {
        output << "[ERROR] " << error.what() << '\n';
      }
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
