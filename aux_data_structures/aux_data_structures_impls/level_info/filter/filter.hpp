#pragma once
#include "bloom_filter.hpp"

template<unsigned int num_elements>
class Filter {
    private:
    BloomFilter filter;

    public:
    Filter() {
        /* num_elements + 1 to additionally support null key for fast insertion in request 
        segment
        */
        bloom_filter_init(&filter, num_elements + 1, FILTER_FALSE_POS_PROB);
    }

    void insert_key(char* key) {
        bloom_filter_add_string(&filter, key);
    }

    void delete_key(char* key) {}

    bool prob_contains_key(char* key) {
        return bloom_filter_check_string(&filter, key) != BLOOM_FAILURE;
    }

    bool empty() {
        return !filter.elements_added;
    }
};