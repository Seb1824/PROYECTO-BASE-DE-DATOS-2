#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "index/extensible_hash_table.h"
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
using minisgbd::QueryExecutor;
using minisgbd::QueryPlanType;
using minisgbd::QueryProfiler;
using minisgbd::TableHeap;

struct BenchResult {
  int n{0};
  std::string mode;
  std::string plan;
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
    const int value = key * 10;
    if (!table_heap.Insert(key, value)) {
      throw std::runtime_error(
          "No se pudo cargar un registro en TableHeap.");
    }
    if (hash_index != nullptr && !hash_index->Insert(key, value)) {
      throw std::runtime_error(
          "No se pudo cargar un registro en el indice.");
    }
  }
  bpm.FlushAllPages();
  const auto end = Clock::now();

  return std::chrono::duration<double, std::milli>(end - start).count();
}

BenchResult RunSearchBenchmark(int n, const std::vector<int> &keys,
                               int requested_queries,
                               std::size_t pool_size, bool with_index) {
  const std::string mode = with_index ? "con_indice" : "sin_indice";
  const std::string db_file =
      "benchmark_" + mode + "_" + std::to_string(n) + ".db";
  const std::vector<int> search_keys =
      SelectSearchKeys(keys, requested_queries);

  BenchResult result;
  result.n = n;
  result.mode = mode;
  result.plan = with_index ? "IndexScan" : "Filter+SeqScan";
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

    QueryExecutor executor("registros", &table_heap, hash_index.get());
    QueryProfiler profiler(&executor, &bpm, &disk_manager);

    for (int key : search_keys) {
      const ProfiledQueryResult query_result = profiler.Execute(
          "SELECT * FROM registros WHERE id = " + std::to_string(key) + ";");

      const QueryPlanType expected_plan =
          with_index ? QueryPlanType::kIndexScan
                     : QueryPlanType::kFilteredSeqScan;
      if (query_result.plan_type != expected_plan) {
        throw std::runtime_error(
            "QueryExecutor selecciono un plan inesperado.");
      }
      if (query_result.rows.size() != 1 ||
          query_result.rows.front().key != key) {
        throw std::runtime_error(
            "El benchmark no recupero la fila esperada.");
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
            << " plan=" << result.plan
            << " consultas=" << result.query_count
            << " encontradas=" << result.rows_found
            << " insert_ms=" << result.insert_ms
            << " search_ms=" << result.search_ms
            << " io=" << result.io_operations << '\n';
  return result;
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

void WriteResult(std::ofstream &csv, const BenchResult &result) {
  csv << result.n << ',' << result.mode << ',' << result.plan << ','
      << result.query_count << ',' << result.rows_found << ','
      << result.insert_ms << ',' << result.search_ms << ','
      << result.hits << ',' << result.misses << ','
      << result.hit_ratio << ',' << result.disk_reads << ','
      << result.disk_writes << ',' << result.io_operations << ','
      << result.db_bytes << '\n';
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
    constexpr std::size_t pool_size = 10;

    if (output_path.has_parent_path()) {
      std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream csv(output_path);
    if (!csv.is_open()) {
      throw std::runtime_error(
          "No se pudo crear el archivo CSV del benchmark.");
    }
    csv << std::fixed << std::setprecision(6);
    csv << "n,modo,plan,queries,rows_found,insert_ms,search_ms,"
           "hits,misses,hit_ratio,disk_reads,disk_writes,"
           "io_operations,db_bytes\n";

    for (int n : BuildSizes(maximum_size)) {
      const std::vector<int> keys = GenerateUniqueKeys(n, 42);
      WriteResult(csv, RunSearchBenchmark(
                           n, keys, requested_queries, pool_size, true));
      WriteResult(csv, RunSearchBenchmark(
                           n, keys, requested_queries, pool_size, false));
      csv.flush();
      std::cout << '\n';
    }

    std::cout << "Resultados guardados en " << output_path.string() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[ERROR] " << error.what() << '\n';
    return 1;
  }
}
