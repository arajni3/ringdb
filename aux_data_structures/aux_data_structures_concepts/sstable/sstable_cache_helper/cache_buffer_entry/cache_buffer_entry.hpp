#pragma once
#include <concepts>

template<typename CacheBufferEntry>
concept CacheBufferEntryLike = requires(
    CacheBufferEntry cache_buffer_entry) {
        requires std::same_as<char&, decltype(*(cache_buffer_entry.key))>;
        requires std::same_as<char&, decltype(*(cache_buffer_entry.value))>;
};