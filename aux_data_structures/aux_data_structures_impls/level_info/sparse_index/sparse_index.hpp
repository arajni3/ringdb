#pragma once
#include <cstring>
#include <algorithm>

struct SparseIndex {
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
        return i;
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
    char null_key[KEY_LENGTH];
};