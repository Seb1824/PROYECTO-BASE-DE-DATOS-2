#include "storage/disk_manager.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace minisgbd {

DiskManager::DiskManager(const std::string &db_file_path)
    : file_name_(db_file_path), next_page_id_(0) {
  db_io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);

  if (!db_io_.is_open()) {
    db_io_.clear();
    db_io_.open(file_name_, std::ios::out | std::ios::binary);
    db_io_.close();
    db_io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);

    if (!db_io_.is_open()) {
      throw std::runtime_error(
          "DiskManager: no se pudo abrir ni crear el archivo de base de datos '" +
          file_name_ + "'");
    }
  }

  db_io_.seekg(0, std::ios::end);
  std::streampos file_size = db_io_.tellg();
  if (file_size == static_cast<std::streampos>(-1)) {
    throw std::runtime_error(
        "DiskManager: error al obtener el tamaño de '" + file_name_ + "'");
  }

  next_page_id_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
}

DiskManager::~DiskManager() { shutdown(); }

void DiskManager::shutdown() {
  std::lock_guard<std::mutex> lock(db_io_latch_);
  if (db_io_.is_open()) {
    db_io_.flush();
    db_io_.close();
  }
}

void DiskManager::ensure_capacity(page_id_t page_id) {
  std::streamoff target_end =
      static_cast<std::streamoff>(page_id + 1) * PAGE_SIZE;

  db_io_.seekp(0, std::ios::end);
  std::streampos file_size = db_io_.tellp();

  if (static_cast<std::streamoff>(file_size) >= target_end) {
    return;
  }

  static thread_local char zeros[PAGE_SIZE] = {0};
  std::streamoff remaining =
      target_end - static_cast<std::streamoff>(file_size);

  db_io_.seekp(file_size);
  while (remaining > 0) {
    std::streamsize chunk = static_cast<std::streamsize>(
        std::min<std::streamoff>(remaining, PAGE_SIZE));
    db_io_.write(zeros, chunk);
    if (db_io_.fail()) {
      db_io_.clear();
      throw std::runtime_error(
          "DiskManager: error de I/O al extender el archivo '" + file_name_ +
          "'");
    }
    remaining -= chunk;
  }
  db_io_.flush();
}

void DiskManager::read_page(page_id_t page_id, char *page_data) {
  if (page_id == INVALID_PAGE_ID || page_id < 0) {
    throw std::invalid_argument("DiskManager::read_page: page_id inválido");
  }

  std::lock_guard<std::mutex> lock(db_io_latch_);

  std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;

  db_io_.seekg(0, std::ios::end);
  std::streampos file_size = db_io_.tellg();

  if (offset >= static_cast<std::streamoff>(file_size)) {
    std::memset(page_data, 0, PAGE_SIZE);
    return;
  }

  db_io_.seekg(offset);
  db_io_.read(page_data, PAGE_SIZE);

  if (db_io_.fail() && !db_io_.eof()) {
    db_io_.clear();
    throw std::runtime_error(
        "DiskManager: error de I/O al leer la página " +
        std::to_string(page_id));
  }

  std::streamsize read_count = db_io_.gcount();
  db_io_.clear();

  if (read_count < PAGE_SIZE) {
    std::memset(page_data + read_count, 0,
                PAGE_SIZE - static_cast<size_t>(read_count));
  }

  ++read_count_;
}

void DiskManager::write_page(page_id_t page_id, const char *page_data) {
  if (page_id == INVALID_PAGE_ID || page_id < 0) {
    throw std::invalid_argument("DiskManager::write_page: page_id inválido");
  }

  std::lock_guard<std::mutex> lock(db_io_latch_);

  ensure_capacity(page_id);

  std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
  db_io_.seekp(offset);
  db_io_.write(page_data, PAGE_SIZE);

  if (db_io_.fail()) {
    db_io_.clear();
    throw std::runtime_error(
        "DiskManager: error de I/O al escribir la página " +
        std::to_string(page_id));
  }

  db_io_.flush();

  if (page_id >= next_page_id_) {
    next_page_id_ = page_id + 1;
  }

  ++write_count_;
}

page_id_t DiskManager::allocate_page() {
  std::lock_guard<std::mutex> lock(db_io_latch_);

  page_id_t new_page_id = next_page_id_;
  next_page_id_++;

  std::streamoff offset = static_cast<std::streamoff>(new_page_id) * PAGE_SIZE;
  static thread_local char zeros[PAGE_SIZE] = {0};

  db_io_.seekp(offset);
  db_io_.write(zeros, PAGE_SIZE);

  if (db_io_.fail()) {
    db_io_.clear();
    throw std::runtime_error(
        "DiskManager: error de I/O al asignar una nueva página");
  }

  db_io_.flush();
  ++write_count_;

  return new_page_id;
}

int DiskManager::get_num_pages() const {
  std::lock_guard<std::mutex> lock(db_io_latch_);
  return static_cast<int>(next_page_id_);
}

uint64_t DiskManager::GetReadCount() const {
  std::lock_guard<std::mutex> lock(db_io_latch_);
  return read_count_;
}

uint64_t DiskManager::GetWriteCount() const {
  std::lock_guard<std::mutex> lock(db_io_latch_);
  return write_count_;
}

}  // namespace minisgbd


