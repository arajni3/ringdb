#pragma once
#include <concepts>
#include <cstddef>

// Array-based stack for traversing request batches.
template<typename Stack>
concept StackLike = requires(Stack s, unsigned int desired_sstable_offset, 
unsigned int cur_request, unsigned int left, unsigned int right) {
    /* in each quadruplet entry, first element will be the desired sstable offset, 
    second will be the current request index, third will be the left boundary, and fourth will 
    be the right boundary
    */
    requires std::same_as<unsigned int(&)[4], decltype(*(s.data))>;

    {s.push_top(desired_sstable_offset, cur_request, left, right)} -> std::same_as<void>;
    {s.empty()} -> std::same_as<bool>;
    {s.top()} -> std::same_as<unsigned int(&)[4]>;
    {s.pop_top()} -> std::same_as<void>;

    requires std::default_initializable<Stack>;
};