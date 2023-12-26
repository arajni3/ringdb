#pragma once
#include <concepts>
#include "../../sstable/request_batch/request_batch.hpp"

/* A struct allocated on the stack consisting of an array of length 
LEVEL_FACTOR + 1 of heap-allocated RequestBatches. The last request batch 
is for read requests and the last one is meant to be moved to the level after 
the current next level.
*/
template<typename Decomposition>
concept DecompositionLike = requires(Decomposition decomp) {
    requires RequestBatchLike<std::remove_reference_t<decltype(**(decomp.decomposition))>>;

    // does not include the potential extra request batch mentioned above
    requires std::same_as<unsigned int, decltype(decomp.num_req_batches)>;
    
    requires std::same_as<unsigned int, decltype(decomp.total_decomp_size)>;
    requires std::same_as<unsigned int&, decltype(*(decomp.expected_sstable_sizes))>;
};