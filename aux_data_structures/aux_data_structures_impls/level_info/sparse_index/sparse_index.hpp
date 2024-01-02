#pragma once
#include <cstring>
#include <algorithm>

template<std::size_t old_size>
consteval unsigned int sparse_index_align() {
    std::size_t res = std::lcm(old_size, ALIGN_NO_FALSE_SHARING);
    std::size_t power = 1;
    while (power < res) {
        power <<= 1;
    }
    return power;
}

template<unsigned int struct_size>
struct alignas(sparse_index_align<struct_size>()) SparseIndex {
    char index[(KEY_LENGTH << 1) * LEVEL_FACTOR];

    SparseIndex() {
        std::fill(index, index + (KEY_LENGTH << 1) * LEVEL_FACTOR, '\0');
        std::fill(null_key, null_key + KEY_LENGTH, '\0');
    }

    void insert_range(char* min_key, char* max_key, unsigned int table_num) {
        char* offset_ptr = index + (KEY_LENGTH << 1) * table_num;
        std::memcpy(offset_ptr, min_key, KEY_LENGTH);
        std::memcpy(offset_ptr + KEY_LENGTH, max_key, KEY_LENGTH);
    }

    void remove_table(unsigned int table_num) {
        char* offset_ptr = index + (KEY_LENGTH << 1) * table_num;
        std::fill(offset_ptr, offset_ptr + KEY_LENGTH, '\0');
        std::fill(offset_ptr + KEY_LENGTH, offset_ptr + (KEY_LENGTH << 1), '\0');
    }

    unsigned int get_table_num(char* key) {
        int right_res; 
        unsigned int i;
        char* offset_ptr;
        for (i = 0; i < LEVEL_FACTOR; i += KEY_LENGTH << 1) {
            offset_ptr = index + i;
            if (!memcmp(offset_ptr, null_key, KEY_LENGTH)) {
                std::memcpy(offset_ptr, key, KEY_LENGTH);
                break;
            }

            if (memcmp(offset_ptr, key, KEY_LENGTH) <= 0 && (right_res = 
            memcmp(offset_ptr + KEY_LENGTH, key, KEY_LENGTH) >= 0)) {
                if (!memcmp(offset_ptr + KEY_LENGTH, null_key, KEY_LENGTH)) {
                    std::memcpy(offset_ptr + KEY_LENGTH, key, KEY_LENGTH);
                }
                return i;
            }
        }
        return -1;
    }

    char* get_table_min_key(unsigned int table_num) {
        return index + (KEY_LENGTH << 1) * table_num;
    }
    
    char* get_table_max_key(unsigned int table_num) {
        return index + (KEY_LENGTH << 1) * table_num + KEY_LENGTH;
    }

    void set_table_min_key(unsigned int table_num, char* key) {
        std::memcpy(index + (KEY_LENGTH << 1) * table_num, key, KEY_LENGTH);
    }

    void set_table_max_key(unsigned int table_num, char* key) {
        std::memcpy(index + (KEY_LENGTH << 1) * table_num + KEY_LENGTH, key, KEY_LENGTH);
    }

    private:
    char* null_key = new char[KEY_LENGTH];
};