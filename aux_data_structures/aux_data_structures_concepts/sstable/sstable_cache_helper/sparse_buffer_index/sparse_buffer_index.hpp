#pragma once
#include <concepts>
#include "../cache_location.hpp"

/* Internal data structure. The main internal data structure (the cache locations array) can 
be represented as a sorted array of (non-overlapping) intervals, and the array of sparse buffer 
index locations can be represented as a simple array indexed by buffer ids since the buffer ids 
will always be in the range [0, max number of buffers] where the maximum number of buffers is 
defined as a global constant.
*/
template<typename SparseBufferIndex>
concept SparseBufferIndexLike = requires(
    SparseBufferIndex sparse_buffer_index,
    std::size_t desired_offset, unsigned int buffer_id) {
        requires CacheLocationLike<std::remove_reference_t<
        decltype(*sparse_buffer_index.cache_locations)>>;
        requires std::same_as<unsigned int&, 
        decltype(*(sparse_buffer_index.sparse_buffer_index_locations))>;

        /* offset from which there are definitely no sstable entries, that is,
        sparse_buffer_index.cur_min_invalid_offset >= actual current sstable size
        */
        requires std::same_as<std::size_t, decltype(
            sparse_buffer_index.cur_min_invalid_offset)>;

        /* buffer id of the buffer that was most recently used by io_uring; it is 
        always contiguous, so it can be decremented by 1 to get the previous selected
        buffer
        */
        requires std::same_as<unsigned int, decltype(
            sparse_buffer_index.id_of_most_recently_selected_buffer
        )>;
};