#pragma once
#include <concepts>

struct CacheLocation {
    std::size_t sstable_offset_boundary[2];
    unsigned int buffer_id;

    CacheLocation(std::size_t left_boundary, std::size_t right_boundary, unsigned int buffer_id) {
        sstable_offset_boundary[0] = left_boundary;
        sstable_offset_boundary[1] = right_boundary;
        this->buffer_id = buffer_id;
    }

    CacheLocation() {}
};