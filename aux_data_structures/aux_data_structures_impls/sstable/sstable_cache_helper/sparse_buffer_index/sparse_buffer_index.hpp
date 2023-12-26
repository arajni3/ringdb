#pragma once
#include "../../../../aux_data_structures_concepts/sstable/sstable_cache_helper/cache_location.hpp"

template<CacheLocationLike CacheLocation, unsigned int max_num_buffers>
struct SparseBufferIndex {
    CacheLocation cache_locations[max_num_buffers];
    unsigned int sparse_buffer_index_locations[max_num_buffers];
    std::size_t cur_min_invalid_offset;
    unsigned int id_of_most_recently_selected_buffer;
};