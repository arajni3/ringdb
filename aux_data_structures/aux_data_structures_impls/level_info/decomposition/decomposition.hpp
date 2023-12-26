#pragma once
#include "../../../aux_data_structures_concepts/sstable/request_batch/request_batch.hpp"

template<RequestBatchLike RequestBatch>
struct Decomposition {
    RequestBatch* decomposition[LEVEL_FACTOR + 1];
    unsigned int expected_sstable_sizes[LEVEL_FACTOR];
    unsigned int num_req_batches;
    unsigned int total_decomp_size;
};