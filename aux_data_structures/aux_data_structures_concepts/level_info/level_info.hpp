#pragma once
#include <concepts>
#include <atomic>
#include "filter/filter.hpp"
#include "sparse_index/sparse_index.hpp"
#include "decomposition/decomposition.hpp"
#include "buffer_queue/buffer_queue.hpp"
#include "../sstable/sstable_info.hpp"
#include "../sstable/request_batch/request_batch.hpp"

template<typename LevelInfo>
concept LevelInfoLike = requires(LevelInfo level_info, bool is_single_thread) {
    // this and the next field should be packed into a named struct named "guard"
    std::same_as<std::atomic_uchar, decltype(level_info.guard.atomic_guard)>;
    // Field that tells whether this level is accessed by only one thread ever.
    std::same_as<bool, decltype(level_info.guard.is_single_thread)>;

    {level_info.set_is_single_thread(is_single_thread)} -> std::same_as<void>;

    requires FilterLike<std::remove_reference_t<decltype(*level_info.filters)>>;
    requires BufferQueueLike<std::remove_reference_t<decltype(*level_info.buffer_queues)>>;
    requires SparseIndexLike<decltype(level_info.sparse_index)>;
    requires SSTableInfoLike<std::remove_reference_t<decltype(*(level_info.sstable_infos))>>;
};

template<typename LevelInfo, typename RequestBatch>
concept LevelInfoDecompose = requires(
    LevelInfo level_info, 
    RequestBatch* req_batch
    ) {
        LevelInfoLike<LevelInfo>;
        RequestBatchLike<RequestBatch>;

        /* If compacting, the decomposition method should insert all the keys of an sstable's 
        request batch's nonempty request segment into the sstable's bloom filter
        */
        requires DecompositionLike<std::remove_reference_t<
        decltype(level_info.decompose(req_batch))>>;

        std::default_initializable<LevelInfo>;
    };