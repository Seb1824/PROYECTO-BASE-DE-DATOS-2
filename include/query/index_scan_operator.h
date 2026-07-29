#pragma once

#include "index/extensible_hash_table.h"
#include "buffer/buffer_pool_manager.h"
#include <vector>
#include <cstdint>

namespace minisgbd {

struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t slot_num{0};
};

class IndexScanOperator {
public:
    IndexScanOperator(ExtensibleHashTable *hash_index, BufferPoolManager *bpm, int search_key);
    ~IndexScanOperator() = default;

    void Open();
    bool Next(RID *rid, int *value);
    void Close();

private:
    ExtensibleHashTable *hash_index_;
    BufferPoolManager *bpm_;
    int search_key_;
    
    std::vector<RID> results_;
    size_t cursor_{0};
    bool initialized_{false};
};

} 