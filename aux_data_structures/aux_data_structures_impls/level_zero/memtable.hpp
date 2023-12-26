#include <cstring>
#include <algorithm>
#include "../../aux_data_structures_concepts/level_info/sparse_index/sparse_index.hpp"
#define MEMTABLE_ENTRY_LENGTH (KEY_LENGTH + VALUE_LENGTH)

template<SparseIndexLike SparseIndex>
struct MemTable {
    char data[(1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20)];
    unsigned int size = 0;
    MemTable() {
        std::fill(data, data + (1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20), '\0');
    }

    bool read(char* key, char* value) {
        unsigned int left = 0, right = size, mid;
        while (left < right) {
            mid = (left + right) >> 1;
            if (memcmp(data + mid, key, KEY_LENGTH) < 0) {
                left = mid + MEMTABLE_ENTRY_LENGTH;
            } else {
                right = mid;
            }
        }
        char* src = data + left;
        if (left < size && !memcmp(src, key, KEY_LENGTH)) {
            std::memcpy(value, src + KEY_LENGTH, VALUE_LENGTH);
            return true;
        }
        return false;
    }

    void write(char* key, char* value, unsigned int table_num, 
    SparseIndex& memtable_sparse_index) {
        unsigned int left = 0, right = size, mid;
        while (left < right) {
            mid = (left + right) >> 1;
            if (memcmp(data + mid, key, KEY_LENGTH) < 0) {
                left = mid + MEMTABLE_ENTRY_LENGTH;
            } else {
                right = mid;
            }
        }
        
        char* dest = data + left;
        if (left == size) {
            size += MEMTABLE_ENTRY_LENGTH;
            std::memcpy(dest + KEY_LENGTH, value, VALUE_LENGTH);
            memtable_sparse_index.set_table_max_key(table_num, value);
        } else {
            if (!left) {
                memtable_sparse_index.set_table_min_key(table_num, value);
            }
            char buffer1[MEMTABLE_ENTRY_LENGTH];
            char buffer2[MEMTABLE_ENTRY_LENGTH];
            std::memcpy(buffer2, key, KEY_LENGTH);
            std::memcpy(buffer2 + KEY_LENGTH, value, VALUE_LENGTH);
            unsigned int i;
            for (i = left; i < size; i += 1, dest += MEMTABLE_ENTRY_LENGTH) {
                std::memcpy(buffer1, dest, MEMTABLE_ENTRY_LENGTH);
                std::memcpy(dest, buffer2, MEMTABLE_ENTRY_LENGTH);
                std::memcpy(buffer2, buffer1, MEMTABLE_ENTRY_LENGTH);
            }
            std::memcpy(dest, buffer2, MEMTABLE_ENTRY_LENGTH);
            size += MEMTABLE_ENTRY_LENGTH;
        }
    }

    void empty_out() {
        std::fill(data, data + (1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20), '\0');
    }
};