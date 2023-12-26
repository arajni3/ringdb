#pragma once
#include <concepts>

template<typename CacheLocation>
concept CacheLocationLike = requires(CacheLocation cache_location) {
    requires std::same_as<unsigned int, decltype(cache_location.buffer_id)>;
    requires std::same_as<std::size_t[2], decltype(cache_location.sstable_offset_boundary)>;
    requires std::constructible_from<CacheLocation, std::size_t, std::size_t, unsigned int>;
    /* Default constructor will be used for unsuccessful cache search (not found);
    the offset boundaries for not found should be equal, and should be -1 if the 
    desired sstable offset does not exist in the cache (the min_cur_invalid_offset field 
    will be queried before the sparse buffer index will be searched). Internally in the sparse 
    buffer index and cache helper, a buffer_id of -1 (the maximum unsigned int value) should be 
    used to indicate that the cache location in the sparse buffer index has been removed (hence is 
    effectively a tombstone).
    */
    requires std::default_initializable<CacheLocation>;
};