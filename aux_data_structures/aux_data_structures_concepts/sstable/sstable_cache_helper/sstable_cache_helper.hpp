#pragma once
#include <concepts>
#include "sparse_buffer_index/sparse_buffer_index.hpp"
#include "cache_buffer_entry/cache_buffer_entry.hpp"
 
/* Structured page cache helper for an sstable mapping keys to sstable file offsets. 
The actual page cache buffers will be stored as a local variable in the sstable's 
pthread function. Because contiguous chunks of an sstable will not generally be part 
of a single subtree in the sstable, we need to find the sstable offset of a key by 
searching from the top of the sstable. 

To do this, we need to be able to map sstable offsets to buffers starting from offset 
0. A buffer can be located quickly using its buffer id. We can internally use a sparse 
buffer index that maps an sstable offset to the buffer id of the buffer in the page 
cache whose offset boundary contains the former offset; we explicitly need said offset 
boundary so that we can later compute the next desired offset, so the sparse buffer 
index should explicitly map desired offsets to CacheLocations. 

When we read a chunk from the sstable, we can get the chunk's offset boundary using the
offset we requested and the resulting number of bytes read. We also need to be able to 
remove buffers quickly, so we should store another sub-index in the sparse buffer index that maps 
a buffer id to its location in the first sparse index, similar to how an LRU cache uses a
hash table to quickly find locations of linked list nodes.
*/
template<typename SSTableCacheHelper>
concept SSTableCacheHelperLike = requires(
    SSTableCacheHelper sstable_cache_helper, 
    unsigned int buffer_id,
    unsigned int buffer_id2,
    std::size_t desired_sstable_offset,
    std::size_t sstable_offset1,
    std::size_t sstable_offset2) {

        // initialize the sparse buffer index in the cache helper's (default) constructor
        requires std::default_initializable<SSTableCacheHelper>;

        requires SparseBufferIndexLike<
        decltype(sstable_cache_helper.sparse_buffer_index)
        >;

        requires std::same_as<unsigned int, decltype(sstable_cache_helper.free_buffers_left)>;
        requires std::same_as<unsigned int, 
        decltype(sstable_cache_helper.id_of_least_recently_selected_buffer)>;

        /* If this directly returns a constructor call, then RVO will apply. The
        left sstable boundary should be -1 if no buffer contains this offset.
        */
        {sstable_cache_helper.get_buffer_info(desired_sstable_offset)} 
        -> CacheLocationLike;

        /* Set the cur_min_invalid_offset field of the sparse buffer index
        to the given offset; this method will be called only when a read does not fill a 
        buffer, in which case the offset represented by the end of the buffer is the new 
        minimum invalid sstable offset (and will be the argument to this method) because 
        a read will be scheduled from storage or in memory unless the desired sstable offset 
        is less than the current minimum invalid sstable offset.
        */
        {sstable_cache_helper.set_cur_min_invalid_offset(desired_sstable_offset)}
        -> std::same_as<void>;

        {sstable_cache_helper.get_cur_min_invalid_offset()} -> std::same_as<std::size_t>;

        /* use the id_of_most_recently_selected_buffer field to implement this method
        */
        {sstable_cache_helper.replenish_least_recently_selected_buffer()} 
        -> std::same_as<void>;

        /* Map the given buffer id to the given offset boundary
        and set set the id_of_most_recently_selected_buffer field of the sparse buffer
        index to the given buffer id. Also, increment the the 
        id_of_most_recently_selected_buffer field of the sparse index by
        the appropriate amount if needed.
        */
        {sstable_cache_helper.map_buffer_to_offset_boundary(buffer_id, sstable_offset1,
        sstable_offset2)} -> std::same_as<void>;

        /* simply increment the number of free buffers
        */
        {sstable_cache_helper.add_buffer()} -> std::same_as<void>;

        /* will actually remove only buffers whose ids are in the range 
        [buffer_id, sparse_buffer_index.id_of_most_recently_selected_buffer]
        */
        {sstable_cache_helper.remove_buffer_range(buffer_id, buffer_id2)} -> std::same_as<void>;

        {sstable_cache_helper.get_id_of_most_recently_selected_buffer()} 
        -> std::same_as<unsigned int>;
};