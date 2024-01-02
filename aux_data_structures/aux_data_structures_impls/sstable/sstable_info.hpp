#pragma once
#include <liburing.h>
#include "../../aux_data_structures_concepts/sstable/request_batch/request_batch.hpp"
#include "../../aux_data_structures_concepts/sstable/request_batch/request_batch_wait_queue.hpp"
#include "../../aux_data_structures_concepts/sstable/sstable_cache_helper/sstable_cache_helper.hpp"
#include "../../aux_data_structures_concepts/sstable/stack/stack.hpp"
#include <numeric>

template<unsigned int alignment>
consteval unsigned int sstable_align() {
    unsigned int res = std::lcm(alignment, ALIGN_NO_FALSE_SHARING), power = 1;
    while (power < 1) {
        power <<= 1;
    }
    return power;
}

// this data structure is aligned to avoid false sharing
template<RequestBatchLike RequestBatch, RequestBatchWaitQueueLike RequestBatchWaitQueue, 
SSTableCacheHelperLike SSTableCacheHelper, StackLike Stack, int file_path_length, 
unsigned int max_num_buffers, std::size_t alignment>
struct alignas(sstable_align<alignment>()) SSTableInfo {
    RequestBatchWaitQueue req_batch_wq;
    char file_path[file_path_length];
    bool is_flushed_to = false;
    unsigned int insert_buffers_from = 0;
    unsigned int buffer_ring_id;
    char* page_cache_buffers[max_num_buffers];
    char min_key[KEY_LENGTH];
    char max_key[KEY_LENGTH];
    struct io_uring* io_ring;
    struct io_uring_buf_ring* buffer_ring;
    SSTableCacheHelper cache_helper;
    Stack stack;
    RequestBatch* req_batch;
    bool waiting_on_io = false;
    bool already_waited = false;
    std::size_t desired_sstable_offset = -2;
};