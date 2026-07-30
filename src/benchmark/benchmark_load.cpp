#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
#include "query/parser.h"
#include "query/query_executor.h"
#include "query/query_profiler.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"

namespace {

using Clock = std::chrono::steady_clock;
using minisgbd::BufferPoolManager;
using minisgbd::CARReplacer;
using minisgbd::DiskManager;
using minisgbd::ExtensibleHashTable;
using minisgbd::ProfiledQueryResult;
using minisgbd::Parser;
using minisgbd::PersonRecord;
using minisgbd::QueryExecutor;
using minisgbd::QueryPlanType;
using minisgbd::QueryProfiler;
using minisgbd::RID;
using minisgbd::TableHeap;

PersonRecord BuildBenchmarkPerson(int id) {
  static const std::vector<std::string> cities{
      "Lima", "Arequipa", "Cusco", "Trujillo", "Piura"};
  static const std::vector<std::string> professions{
      "Ingenieria", "Medicina", "Arquitectura",
      "Derecho", "Analisis de Datos"};
  return PersonRecord{
      id,
      "Persona " + std::to_string(id),
      cities[static_cast<std::size_t>(id) % cities.size()],
      professions[static_cast<std::size_t>(id) %
                  professions.size()],
  };
}

struct BenchResult {
  int n{0};
  std::string mode;
  std::string plan;
  int repetition{0};
  int query_count{0};
  int rows_found{0};
  double insert_ms{0.0};
  double search_ms{0.0};
  uint64_t hits{0};
  uint64_t misses{0};
  double hit_ratio{0.0};
  uint64_t disk_reads{0};
  uint64_t disk_writes{0};
  uint64_t io_operations{0};
  uintmax_t db_bytes{0};
};

struct MetricSummary {
  double mean{0.0};
  double standard_deviation{0.0};
};

struct BenchSummary {
  int n{0};
  std::string mode;
  std::string plan;
  int repetitions{0};
  int query_count{0};
  MetricSummary insert_ms;
  MetricSummary search_ms;
  MetricSummary hits;
  MetricSummary misses;
  MetricSummary hit_ratio;
  MetricSummary disk_reads;
  MetricSummary disk_writes;
  MetricSummary io_operations;
  MetricSummary db_bytes;
};

std::vector<int> GenerateUniqueKeys(int count, unsigned int seed) {
  std::vector<int> keys(static_cast<std::size_t>(count));
  std::iota(keys.begin(), keys.end(), 0);
  std::shuffle(keys.begin(), keys.end(), std::mt19937(seed));
  return keys;
}

std::vector<int> SelectSearchKeys(const std::vector<int> &keys,
                                  int requested_count) {
  const int count =
      std::min(requested_count, static_cast<int>(keys.size()));
  std::vector<int> selected;
  selected.reserve(static_cast<std::size_t>(count));

  for (int index = 0; index < count; ++index) {
    const std::size_t position =
        static_cast<std::size_t>(index) * keys.size() /
        static_cast<std::size_t>(count);
    selected.push_back(keys[position]);
  }
  return selected;
}

double BuildPhysicalDatabase(const std::string &db_file,
                             const std::vector<int> &keys,
                             std::size_t pool_size, bool with_index) {
  std::filesystem::remove(db_file);

  DiskManager disk_manager(db_file);
  CARReplacer replacer(pool_size);
  BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
  TableHeap table_heap(&bpm);
  std::unique_ptr<ExtensibleHashTable> hash_index;
  if (with_index) {
    hash_index = std::make_unique<ExtensibleHashTable>(&bpm);
  }

  const auto start = Clock::now();
  for (int key : keys) {
    const PersonRecord person = BuildBenchmarkPerson(key);
    const std::optional<RID> rid =
        table_heap.InsertTuple(person);
    if (!rid.has_value()) {
      throw std::runtime_error(
          "No se pudo cargar un registro en TableHeap.");
    }
    if (hash_index != nullptr &&
        !hash_index->Insert(key, rid->Encode())) {
      throw std::runtime_error(
          "No se pudo cargar un RID en el indice.");
    }
  }
  bpm.FlushAllPages();
  const auto end = Clock::now();

  return std::chrono::duration<double, std::milli>(end - start).count();
}

BenchResult RunSearchBenchmark(int n, const std::vector<int> &keys,
                               int requested_queries,
                               std::size_t pool_size, bool with_index,
                               int repetition) {
  const std::string mode = with_index ? "con_indice" : "sin_indice";
  const std::string db_file =
      "benchmark_" + mode + "_" + std::to_string(n) + "_" +
      std::to_string(repetition) + ".db";
  const std::vector<int> search_keys =
      SelectSearchKeys(keys, requested_queries);

  BenchResult result;
  result.n = n;
  result.mode = mode;
  result.plan = with_index ? "IndexScan" : "Filter+SeqScan";
  result.repetition = repetition;
  result.query_count = static_cast<int>(search_keys.size());
  result.insert_ms =
      BuildPhysicalDatabase(db_file, keys, pool_size, with_index);
  result.db_bytes = std::filesystem::file_size(db_file);

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(pool_size);
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    std::unique_ptr<ExtensibleHashTable> hash_index;
    if (with_index) {
      hash_index = std::make_unique<ExtensibleHashTable>(&bpm);
    }

    QueryExecutor executor("personas", &table_heap, hash_index.get());
    QueryProfiler profiler(&executor, &bpm, &disk_manager);

    for (int key : search_keys) {
      const ProfiledQueryResult query_result = profiler.Execute(
          "SELECT * FROM personas WHERE id = " + std::to_string(key) + ";");

      const QueryPlanType expected_plan =
          with_index ? QueryPlanType::kIndexScan
                     : QueryPlanType::kFilteredSeqScan;
      if (query_result.plan_type != expected_plan) {
        throw std::runtime_error(
            "QueryExecutor selecciono un plan inesperado.");
      }
      if (query_result.rows.size() != 1 ||
          query_result.rows.front() != BuildBenchmarkPerson(key)) {
        throw std::runtime_error(
            "El benchmark no recupero la fila fisica esperada.");
      }

      ++result.rows_found;
      result.search_ms += query_result.metrics.elapsed_ms;
      result.hits += query_result.metrics.buffer_hits;
      result.misses += query_result.metrics.buffer_misses;
      result.disk_reads += query_result.metrics.disk_reads;
      result.disk_writes += query_result.metrics.disk_writes;
      result.io_operations += query_result.metrics.io_operations;
    }
  }

  const uint64_t accesses = result.hits + result.misses;
  if (accesses != 0) {
    result.hit_ratio =
        static_cast<double>(result.hits) / static_cast<double>(accesses);
  }
  std::filesystem::remove(db_file);

  std::cout << '[' << result.mode << "] n=" << result.n
            << " repeticion=" << result.repetition
            << " plan=" << result.plan
            << " consultas=" << result.query_count
            << " search_ms=" << result.search_ms
            << " io=" << result.io_operations << '\n';
  return result;
}

MetricSummary CalculateSummary(
    const std::vector<BenchResult> &results,
    const std::function<double(const BenchResult &)> &value) {
  MetricSummary summary;
  if (results.empty()) {
    return summary;
  }

  for (const BenchResult &result : results) {
    summary.mean += value(result);
  }
  summary.mean /= static_cast<double>(results.size());

  if (results.size() > 1) {
    double squared_difference_sum = 0.0;
    for (const BenchResult &result : results) {
      const double difference = value(result) - summary.mean;
      squared_difference_sum += difference * difference;
    }
    summary.standard_deviation = std::sqrt(
        squared_difference_sum /
        static_cast<double>(results.size() - 1));
  }
  return summary;
}

BenchSummary Summarize(const std::vector<BenchResult> &results) {
  if (results.empty()) {
    throw std::invalid_argument(
        "No se puede resumir un benchmark vacio.");
  }

  BenchSummary summary;
  summary.n = results.front().n;
  summary.mode = results.front().mode;
  summary.plan = results.front().plan;
  summary.repetitions = static_cast<int>(results.size());
  summary.query_count = results.front().query_count;
  summary.insert_ms = CalculateSummary(
      results, [](const BenchResult &result) { return result.insert_ms; });
  summary.search_ms = CalculateSummary(
      results, [](const BenchResult &result) { return result.search_ms; });
  summary.hits = CalculateSummary(
      results, [](const BenchResult &result) {
        return static_cast<double>(result.hits);
      });
  summary.misses = CalculateSummary(
      results, [](const BenchResult &result) {
        return static_cast<double>(result.misses);
      });
  summary.hit_ratio = CalculateSummary(
      results, [](const BenchResult &result) { return result.hit_ratio; });
  summary.disk_reads = CalculateSummary(
      results, [](const BenchResult &result) {
        return static_cast<double>(result.disk_reads);
      });
  summary.disk_writes = CalculateSummary(
      results, [](const BenchResult &result) {
        return static_cast<double>(result.disk_writes);
      });
  summary.io_operations = CalculateSummary(
      results, [](const BenchResult &result) {
        return static_cast<double>(result.io_operations);
      });
  summary.db_bytes = CalculateSummary(
      results, [](const BenchResult &result) {
        return static_cast<double>(result.db_bytes);
      });
  return summary;
}

std::vector<int> BuildSizes(int maximum_size) {
  const std::vector<int> standard_sizes = {
      1000, 5000, 10000, 50000, 100000};
  std::vector<int> sizes;
  for (int size : standard_sizes) {
    if (size <= maximum_size) {
      sizes.push_back(size);
    }
  }
  if (sizes.empty() || sizes.back() != maximum_size) {
    sizes.push_back(maximum_size);
  }
  return sizes;
}

int ParsePositiveArgument(const char *text, const std::string &name) {
  try {
    std::size_t parsed = 0;
    const int value = std::stoi(text, &parsed);
    if (parsed != std::string(text).size() || value <= 0) {
      throw std::invalid_argument("fuera de rango");
    }
    return value;
  } catch (const std::exception &) {
    throw std::invalid_argument(name + " debe ser un entero positivo.");
  }
}

void WriteRawResult(std::ofstream &csv, const BenchResult &result) {
  csv << result.n << ',' << result.mode << ',' << result.plan << ','
      << result.repetition << ',' << result.query_count << ','
      << result.rows_found << ',' << result.insert_ms << ','
      << result.search_ms << ',' << result.hits << ',' << result.misses
      << ',' << result.hit_ratio << ',' << result.disk_reads << ','
      << result.disk_writes << ',' << result.io_operations << ','
      << result.db_bytes << '\n';
}

void WriteSummaryMetric(std::ofstream &csv, const MetricSummary &metric) {
  csv << ',' << metric.mean << ',' << metric.standard_deviation;
}

void WriteSummaryResult(std::ofstream &csv, const BenchSummary &result) {
  csv << result.n << ',' << result.mode << ',' << result.plan << ','
      << result.repetitions << ',' << result.query_count;
  WriteSummaryMetric(csv, result.insert_ms);
  WriteSummaryMetric(csv, result.search_ms);
  WriteSummaryMetric(csv, result.hits);
  WriteSummaryMetric(csv, result.misses);
  WriteSummaryMetric(csv, result.hit_ratio);
  WriteSummaryMetric(csv, result.disk_reads);
  WriteSummaryMetric(csv, result.disk_writes);
  WriteSummaryMetric(csv, result.io_operations);
  WriteSummaryMetric(csv, result.db_bytes);
  csv << '\n';
}

double LogPosition(double value, double minimum, double maximum,
                   double start, double length, bool invert) {
  const double safe_value = std::max(value, 0.000001);
  const double safe_minimum = std::max(minimum, 0.000001);
  if (maximum <= safe_minimum) {
    return start + length / 2.0;
  }
  const double ratio =
      (std::log10(safe_value) - std::log10(safe_minimum)) /
      (std::log10(maximum) - std::log10(safe_minimum));
  return invert ? start + length * (1.0 - ratio)
                : start + length * ratio;
}

void WriteSvgChart(const std::filesystem::path &path,
                   const std::vector<BenchSummary> &summaries) {
  std::ofstream svg(path);
  if (!svg.is_open()) {
    throw std::runtime_error("No se pudo crear la grafica SVG.");
  }

  constexpr double width = 1200.0;
  constexpr double height = 620.0;
  svg << std::fixed << std::setprecision(2);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" "
         "height=\"620\" viewBox=\"0 0 1200 620\">\n"
      << "<rect width=\"1200\" height=\"620\" fill=\"#f8fafc\"/>\n"
      << "<text x=\"600\" y=\"38\" text-anchor=\"middle\" "
         "font-family=\"Arial\" font-size=\"22\" font-weight=\"700\" "
         "fill=\"#0f172a\">TableHeap fisico: IndexScan vs Filter + "
         "SeqScan</text>\n";

  auto draw_panel =
      [&svg, &summaries](double left, const std::string &title,
                        const std::string &y_label,
                        const std::function<double(const BenchSummary &)> &mean,
                        const std::function<double(const BenchSummary &)> &stddev,
                        bool show_error_bars) {
        constexpr double top = 90.0;
        constexpr double panel_width = 500.0;
        constexpr double panel_height = 420.0;
        double min_x = static_cast<double>(summaries.front().n);
        double max_x = min_x;
        double min_y = std::numeric_limits<double>::max();
        double max_y = 0.0;
        for (const BenchSummary &summary : summaries) {
          min_x = std::min(min_x, static_cast<double>(summary.n));
          max_x = std::max(max_x, static_cast<double>(summary.n));
          min_y = std::min(min_y, std::max(mean(summary) -
                                               stddev(summary),
                                           0.000001));
          max_y = std::max(max_y, mean(summary) + stddev(summary));
        }
        min_y = std::max(min_y * 0.7, 0.000001);
        max_y = std::max(max_y * 1.4, min_y * 10.0);

        svg << "<rect x=\"" << left << "\" y=\"" << top
            << "\" width=\"" << panel_width << "\" height=\""
            << panel_height << "\" fill=\"white\" stroke=\"#cbd5e1\"/>\n"
            << "<text x=\"" << left + panel_width / 2.0
            << "\" y=\"72\" text-anchor=\"middle\" font-family=\"Arial\" "
               "font-size=\"17\" font-weight=\"700\" fill=\"#1e293b\">"
            << title << "</text>\n"
            << "<text x=\"" << left + panel_width / 2.0
            << "\" y=\"550\" text-anchor=\"middle\" font-family=\"Arial\" "
               "font-size=\"13\" fill=\"#334155\">Numero de registros "
               "(escala log)</text>\n"
            << "<text transform=\"translate(" << left - 48.0 << ' '
            << top + panel_height / 2.0
            << ") rotate(-90)\" text-anchor=\"middle\" "
               "font-family=\"Arial\" font-size=\"13\" fill=\"#334155\">"
            << y_label << "</text>\n";

        for (int grid = 0; grid <= 4; ++grid) {
          const double y = top + panel_height * grid / 4.0;
          const double exponent =
              std::log10(max_y) -
              (std::log10(max_y) - std::log10(min_y)) * grid / 4.0;
          svg << "<line x1=\"" << left << "\" y1=\"" << y
              << "\" x2=\"" << left + panel_width << "\" y2=\"" << y
              << "\" stroke=\"#e2e8f0\" stroke-dasharray=\"4 4\"/>\n"
              << "<text x=\"" << left - 8.0 << "\" y=\"" << y + 4.0
              << "\" text-anchor=\"end\" font-family=\"Arial\" "
                 "font-size=\"11\" fill=\"#64748b\">"
              << std::pow(10.0, exponent) << "</text>\n";
        }

        std::vector<int> displayed_sizes;
        for (const BenchSummary &summary : summaries) {
          if (std::find(displayed_sizes.begin(), displayed_sizes.end(),
                        summary.n) != displayed_sizes.end()) {
            continue;
          }
          displayed_sizes.push_back(summary.n);
          const double x = LogPosition(
              summary.n, min_x, max_x, left, panel_width, false);
          svg << "<line x1=\"" << x << "\" y1=\"" << top
              << "\" x2=\"" << x << "\" y2=\"" << top + panel_height
              << "\" stroke=\"#f1f5f9\"/>\n"
              << "<text x=\"" << x << "\" y=\"530\" text-anchor=\"middle\" "
                 "font-family=\"Arial\" font-size=\"11\" fill=\"#64748b\">"
              << summary.n << "</text>\n";
        }

        for (const std::string &mode :
             std::vector<std::string>{"con_indice", "sin_indice"}) {
          const std::string color =
              mode == "con_indice" ? "#2563eb" : "#dc2626";
          std::ostringstream points;
          for (const BenchSummary &summary : summaries) {
            if (summary.mode != mode) {
              continue;
            }
            const double x = LogPosition(
                summary.n, min_x, max_x, left, panel_width, false);
            const double y = LogPosition(
                mean(summary), min_y, max_y, top, panel_height, true);
            points << x << ',' << y << ' ';
            if (show_error_bars) {
              const double low = LogPosition(
                  std::max(mean(summary) - stddev(summary), min_y),
                  min_y, max_y, top, panel_height, true);
              const double high = LogPosition(
                  mean(summary) + stddev(summary), min_y, max_y,
                  top, panel_height, true);
              svg << "<line x1=\"" << x << "\" y1=\"" << low
                  << "\" x2=\"" << x << "\" y2=\"" << high
                  << "\" stroke=\"" << color << "\"/>\n"
                  << "<line x1=\"" << x - 4.0 << "\" y1=\"" << low
                  << "\" x2=\"" << x + 4.0 << "\" y2=\"" << low
                  << "\" stroke=\"" << color << "\"/>\n"
                  << "<line x1=\"" << x - 4.0 << "\" y1=\"" << high
                  << "\" x2=\"" << x + 4.0 << "\" y2=\"" << high
                  << "\" stroke=\"" << color << "\"/>\n";
            }
            svg << "<circle cx=\"" << x << "\" cy=\"" << y
                << "\" r=\"4\" fill=\"" << color << "\"/>\n";
          }
          svg << "<polyline points=\"" << points.str()
              << "\" fill=\"none\" stroke=\"" << color
              << "\" stroke-width=\"2.5\"/>\n";
        }
      };

  draw_panel(
      80.0, "Tiempo promedio de busqueda",
      "Tiempo total de 100 consultas (ms)",
      [](const BenchSummary &summary) { return summary.search_ms.mean; },
      [](const BenchSummary &summary) {
        return summary.search_ms.standard_deviation;
      },
      true);
  draw_panel(
      660.0, "Costo promedio de I/O",
      "Operaciones de pagina",
      [](const BenchSummary &summary) {
        return summary.io_operations.mean;
      },
      [](const BenchSummary &) { return 0.0; }, false);

  svg << "<line x1=\"420\" y1=\"590\" x2=\"450\" y2=\"590\" "
         "stroke=\"#2563eb\" stroke-width=\"3\"/>"
      << "<text x=\"458\" y=\"595\" font-family=\"Arial\" font-size=\"13\" "
         "fill=\"#334155\">IndexScan</text>"
      << "<line x1=\"570\" y1=\"590\" x2=\"600\" y2=\"590\" "
         "stroke=\"#dc2626\" stroke-width=\"3\"/>"
      << "<text x=\"608\" y=\"595\" font-family=\"Arial\" font-size=\"13\" "
         "fill=\"#334155\">Filter + SeqScan</text>"
      << "<text x=\"1180\" y=\"610\" text-anchor=\"end\" "
         "font-family=\"Arial\" font-size=\"10\" fill=\"#64748b\">"
         "Media y desviacion estandar muestral</text>\n</svg>\n";
  (void)width;
  (void)height;
}

std::filesystem::path RawOutputPath(
    const std::filesystem::path &summary_path) {
  return summary_path.parent_path() /
         (summary_path.stem().string() + "_raw.csv");
}

}  // namespace

int main(int argc, char *argv[]) {
  try {
    const std::filesystem::path output_path =
        argc >= 2 ? argv[1] : "docs/resultados_benchmark.csv";
    const int maximum_size =
        argc >= 3 ? ParsePositiveArgument(argv[2], "max_n") : 100000;
    const int requested_queries =
        argc >= 4 ? ParsePositiveArgument(argv[3], "consultas") : 100;
    const int repetitions =
        argc >= 5 ? ParsePositiveArgument(argv[4], "repeticiones") : 5;
    constexpr std::size_t pool_size = 10;
    (void)Parser::ParseStatement(
        "SELECT * FROM personas WHERE id = 1;");

    if (output_path.has_parent_path()) {
      std::filesystem::create_directories(output_path.parent_path());
    }
    const std::filesystem::path raw_output_path =
        RawOutputPath(output_path);

    std::ofstream summary_csv(output_path);
    std::ofstream raw_csv(raw_output_path);
    if (!summary_csv.is_open() || !raw_csv.is_open()) {
      throw std::runtime_error(
          "No se pudieron crear los CSV del benchmark.");
    }
    summary_csv << std::fixed << std::setprecision(6);
    raw_csv << std::fixed << std::setprecision(6);
    summary_csv
        << "n,modo,plan,repetitions,queries,"
           "insert_ms_mean,insert_ms_stddev,"
           "search_ms_mean,search_ms_stddev,"
           "hits_mean,hits_stddev,misses_mean,misses_stddev,"
           "hit_ratio_mean,hit_ratio_stddev,"
           "disk_reads_mean,disk_reads_stddev,"
           "disk_writes_mean,disk_writes_stddev,"
           "io_operations_mean,io_operations_stddev,"
           "db_bytes_mean,db_bytes_stddev\n";
    raw_csv << "n,modo,plan,repetition,queries,rows_found,insert_ms,"
               "search_ms,hits,misses,hit_ratio,disk_reads,disk_writes,"
               "io_operations,db_bytes\n";

    std::vector<BenchSummary> summaries;
    for (int n : BuildSizes(maximum_size)) {
      const std::vector<int> keys = GenerateUniqueKeys(n, 42);
      for (bool with_index : std::vector<bool>{true, false}) {
        std::vector<BenchResult> results;
        for (int repetition = 1; repetition <= repetitions; ++repetition) {
          BenchResult result = RunSearchBenchmark(
              n, keys, requested_queries, pool_size, with_index,
              repetition);
          WriteRawResult(raw_csv, result);
          results.push_back(result);
        }
        const BenchSummary summary = Summarize(results);
        WriteSummaryResult(summary_csv, summary);
        summaries.push_back(summary);
      }
      summary_csv.flush();
      raw_csv.flush();
      std::cout << '\n';
    }

    const std::filesystem::path svg_path =
        output_path.parent_path() / "comparacion_busqueda.svg";
    WriteSvgChart(svg_path, summaries);
    std::cout << "Resumen guardado en " << output_path.string() << '\n'
              << "Mediciones crudas en " << raw_output_path.string() << '\n'
              << "Grafica SVG en " << svg_path.string() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[ERROR] " << error.what() << '\n';
    return 1;
  }
}
