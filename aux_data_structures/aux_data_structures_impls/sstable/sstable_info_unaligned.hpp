#pragma once
#include <liburing.h>
#include "../../aux_data_structures_concepts/sstable/request_batch/request_batch.hpp"
#include "../../aux_data_structures_concepts/sstable/request_batch/request_batch_wait_queue.hpp"
#include "../../aux_data_structures_concepts/sstable/sstable_cache_helper/sstable_cache_helper.hpp"
#include "../../aux_data_structures_concepts/sstable/stack/stack.hpp"

template<RequestBatchLike RequestBatch, RequestBatchWaitQueueLike RequestBatchWaitQueue, 
SSTableCacheHelperLike SSTableCacheHelper, StackLike Stack, int file_path_length, 
unsigned int max_num_buffers>
struct SSTableInfoUnaligned {
    RequestBatchWaitQueue req_batch_wq;
    char file_path[file_path_length];
    bool is_flushed_to;
    unsigned int insert_buffers_from;
    unsigned int buffer_ring_id;
    char* page_cache_buffers[max_num_buffers];
    char min_key[KEY_LENGTH];
    char max_key[KEY_LENGTH];
    struct io_uring* io_ring;
    SSTableCacheHelper cache_helper;
    Stack stack;
    RequestBatch* req_batch;
    bool waiting_on_io;
    bool already_waited;
    std::size_t desired_sstable_offset;
};