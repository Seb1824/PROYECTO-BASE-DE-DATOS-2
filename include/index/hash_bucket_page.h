#pragma once

#include <utility>
#include "buffer/page.h"

namespace minisgbd {

using KeyType = int;
using ValueType = int;

const int BUCKET_HEADER_SIZE = 8; 
const int BUCKET_ARRAY_SIZE = (PAGE_SIZE - BUCKET_HEADER_SIZE) / (sizeof(KeyType) + sizeof(ValueType));

class HashBucketPage {
 public:
  void Init(int local_depth) {
    local_depth_ = local_depth;
    size_ = 0;
  }

  int GetLocalDepth() const { return local_depth_; }
  void SetLocalDepth(int local_depth) { local_depth_ = local_depth; }
  
  int GetSize() const { return size_; }
  bool IsFull() const { return size_ == BUCKET_ARRAY_SIZE; }
  bool IsEmpty() const { return size_ == 0; }

  KeyType KeyAt(int index) const { return array_[index].first; }
  ValueType ValueAt(int index) const { return array_[index].second; }
  void Clear() { size_ = 0; }

  bool GetValue(KeyType key, ValueType *value) const {
    for (int i = 0; i < size_; i++) {
      if (array_[i].first == key) {
        *value = array_[i].second;
        return true;
      }
    }
    return false;
  }

  bool Insert(KeyType key, ValueType value) {
    if (IsFull()) return false;
    array_[size_].first = key;
    array_[size_].second = value;
    size_++;
    return true;
  }

 private:
  int local_depth_;
  int size_;
  std::pair<KeyType, ValueType> array_[BUCKET_ARRAY_SIZE];
};

} // namespace minisgbd