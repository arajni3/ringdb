#pragma once
#include <concepts>

/* in-memory balanced binary search tree representation of a memtable to be compacted to 
secondary storage; this tree does not store a balancing (balance/color) factor because it is 
built immediately from a memtable, which is a sorted array, without rotations and never changes
once created from said sorted array
*/
template<typename RequestSegment>
concept RequestSegmentLike = requires(
    RequestSegment request_segment, char* memtable_data) {
        requires std::same_as<char*, decltype(request_segment.data)>;
        {request_segment.insert_memtable(memtable_data)} 
        -> std::same_as<void>;
};
