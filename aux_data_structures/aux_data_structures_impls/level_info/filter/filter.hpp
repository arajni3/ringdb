#pragma once
#include <cstddef>
#include "bloom_filter.hpp"

template<unsigned int num_elements>
class Filter {
    private:
    BloomFilter<static_cast<uint64_t>(num_elements), static_cast<double>(FILTER_FALSE_POS_PROB)> 
    filter;

    public:

    void insert_key(char* key) {
        bloom_filter_add_string(&filter, key);
    }

    bool prob_contains_key(char* key) {
        return bloom_filter_check_string(&filter, key) != BLOOM_FAILURE;
    }

    bool empty() {
        return !filter.elements_added;
    }
};