#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "storage/disk_manager.h"
#include "index/extensible_hash_table.h"
#include "index/hash_bucket_page.h"

using namespace minisgbd;
using Clock = std::chrono::high_resolution_clock;

std::vector<int> GenerateUniqueKeys(int n, unsigned seed) {
  std::vector<int> keys(n);
  std::iota(keys.begin(), keys.end(), 0);
  std::shuffle(keys.begin(), keys.end(), std::mt19937(seed));
  return keys;
}

struct BenchResult {
  int n;
  std::string modo;
  double insert_ms;
  double search_ms;
  uint64_t hits;
  uint64_t misses;
  double hit_ratio;
};

BenchResult BenchmarkConIndice(int n, const std::vector<int> &keys, size_t pool_size) {
  const std::string db_file = "bench_con_indice.db";
  std::remove(db_file.c_str());

  DiskManager disk_manager(db_file);
  CARReplacer replacer(pool_size);
  BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
  ExtensibleHashTable hash_table(&bpm);

  auto t0 = Clock::now();
  int insertados = 0;
  for (int k : keys) {
    if (hash_table.Insert(k, k * 10)) insertados++;
  }
  auto t1 = Clock::now();

  int encontrados = 0;
  int value;
  for (int k : keys) {
    if (hash_table.GetValue(k, &value)) encontrados++;
  }
  auto t2 = Clock::now();

  BenchResult r;
  r.n = n;
  r.modo = "con_indice";
  r.insert_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  r.search_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
  r.hits = bpm.GetHitCount();
  r.misses = bpm.GetMissCount();
  r.hit_ratio = bpm.GetHitRatio();

  std::cout << "[con_indice] n=" << n << " insertados=" << insertados
            << " encontrados=" << encontrados
            << " insert_ms=" << r.insert_ms
            << " search_ms=" << r.search_ms << std::endl;

  return r;
}

BenchResult BenchmarkSinIndice(int n, const std::vector<int> &keys, size_t pool_size) {
  const std::string db_file = "bench_sin_indice.db";
  std::remove(db_file.c_str());

  DiskManager disk_manager(db_file);
  CARReplacer replacer(pool_size);
  BufferPoolManager bpm(pool_size, &disk_manager, &replacer);

  std::vector<page_id_t> heap_pages;

  auto t0 = Clock::now();
  Page *current_page = nullptr;
  HashBucketPage *current_bucket = nullptr;
  page_id_t current_page_id = INVALID_PAGE_ID;

  for (int k : keys) {
    if (current_bucket == nullptr || current_bucket->IsFull()) {
      if (current_page != nullptr) {
        bpm.UnpinPage(current_page_id, true);
      }
      current_page = bpm.NewPage(&current_page_id);
      current_bucket = reinterpret_cast<HashBucketPage *>(current_page->get_data());
      current_bucket->Init(0);
      heap_pages.push_back(current_page_id);
    }
    current_bucket->Insert(k, k * 10);
  }
  if (current_page != nullptr) {
    bpm.UnpinPage(current_page_id, true);
  }
  auto t1 = Clock::now();

  int encontrados = 0;
  int value;
  for (int k : keys) {
    bool found = false;
    for (page_id_t pid : heap_pages) {
      Page *page = bpm.FetchPage(pid);
      HashBucketPage *bucket = reinterpret_cast<HashBucketPage *>(page->get_data());
      if (bucket->GetValue(k, &value)) {
        found = true;
        bpm.UnpinPage(pid, false);
        break;
      }
      bpm.UnpinPage(pid, false);
    }
    if (found) encontrados++;
  }
  auto t2 = Clock::now();

  BenchResult r;
  r.n = n;
  r.modo = "sin_indice";
  r.insert_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  r.search_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
  r.hits = bpm.GetHitCount();
  r.misses = bpm.GetMissCount();
  r.hit_ratio = bpm.GetHitRatio();

  std::cout << "[sin_indice] n=" << n << " encontrados=" << encontrados
            << " insert_ms=" << r.insert_ms
            << " search_ms=" << r.search_ms << std::endl;

  return r;
}

int main() {
  std::vector<int> tamanos = {1000, 5000, 10000, 50000, 100000};
  size_t pool_size = 10;

  std::ofstream csv("resultados_benchmark.csv");
  csv << "n,modo,insert_ms,search_ms,hits,misses,hit_ratio\n";

  for (int n : tamanos) {
    auto keys = GenerateUniqueKeys(n, 42);

    BenchResult r1 = BenchmarkConIndice(n, keys, pool_size);
    csv << r1.n << "," << r1.modo << "," << r1.insert_ms << ","
        << r1.search_ms << "," << r1.hits << "," << r1.misses << ","
        << r1.hit_ratio << "\n";

    BenchResult r2 = BenchmarkSinIndice(n, keys, pool_size);
    csv << r2.n << "," << r2.modo << "," << r2.insert_ms << ","
        << r2.search_ms << "," << r2.hits << "," << r2.misses << ","
        << r2.hit_ratio << "\n";

    std::cout << std::endl;
  }

  csv.close();
  std::cout << "Resultados guardados en resultados_benchmark.csv" << std::endl;

  return 0;
}