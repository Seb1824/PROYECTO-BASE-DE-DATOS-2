#include "query/index_scan_operator.h"

namespace minisgbd {

IndexScanOperator::IndexScanOperator(ExtensibleHashTable *hash_index, BufferPoolManager *bpm, int search_key)
    : hash_index_(hash_index), bpm_(bpm), search_key_(search_key) {}

void IndexScanOperator::Open() {
    cursor_ = 0;
    results_.clear();
    
    RID found_rid;
    if (hash_index_->GetValue(search_key_, &found_rid.page_id)) {
        results_.push_back(found_rid);
    }
    
    initialized_ = true;
}

bool IndexScanOperator::Next(RID *rid, int *value) {
    if (!initialized_) {
        return false;
    }

    if (cursor_ < results_.size()) {
        *rid = results_[cursor_];
        *value = search_key_;
        cursor_++;
        return true;
    }

    return false;
}

void IndexScanOperator::Close() {
    initialized_ = false;
    results_.clear();
    cursor_ = 0;
}

} 