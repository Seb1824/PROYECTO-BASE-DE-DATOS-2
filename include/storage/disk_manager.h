#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace minisgbd {

using page_id_t = int32_t;

constexpr int PAGE_SIZE = 4096;

constexpr page_id_t INVALID_PAGE_ID = -1;

class DiskManager {
 public:
  explicit DiskManager(const std::string &db_file_path);

  ~DiskManager();

  DiskManager(const DiskManager &) = delete;
  DiskManager &operator=(const DiskManager &) = delete;

  void read_page(page_id_t page_id, char *page_data);

  void write_page(page_id_t page_id, const char *page_data);

  page_id_t allocate_page();

  void shutdown();

  int get_num_pages() const;

 private:
  void ensure_capacity(page_id_t page_id);

  std::string file_name_;
  std::fstream db_io_;
  mutable std::mutex db_io_latch_;
  page_id_t next_page_id_;
};

}
