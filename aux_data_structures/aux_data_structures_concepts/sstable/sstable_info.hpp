#pragma once
#include <concepts>
#include <atomic>
#include <liburing.h>
#include "request_batch/request_batch.hpp"
#include "request_batch/request_batch_wait_queue.hpp"
#include "sstable_cache_helper/sstable_cache_helper.hpp"
#include "stack/stack.hpp"

/* Data structure for holding the entire asynchronous I/O state of an sstable. Needs to be 
aligned to avoid false sharing.
*/
template<typename SSTableInfo>
concept SSTableInfoLike = requires(SSTableInfo sstable_info) {
    requires std::same_as<char&, decltype(*(sstable_info.file_path))>;
    requires std::same_as<bool, decltype(sstable_info.is_flushed_to)>;
    requires std::same_as<unsigned int, decltype(sstable_info.insert_buffers_from)>;
    requires std::same_as<unsigned int, decltype(sstable_info.buffer_ring_id)>;
    requires std::same_as<char*, std::remove_reference_t<
    decltype(*(sstable_info.page_cache_buffers))>>;
    requires std::same_as<char&, decltype(*(sstable_info.min_key))>;
    requires std::same_as<char&, decltype(*(sstable_info.max_key))>;
    requires std::same_as<io_uring&, decltype(*(sstable_info.io_ring))>;
    requires std::same_as<io_uring_buf_ring&, decltype(*(sstable_info.buffer_ring))>;
    requires RequestBatchWaitQueueLike<std::remove_reference_t<
    decltype(sstable_info.req_batch_wq)>>;
    requires SSTableCacheHelperLike<std::remove_reference_t<
    decltype(sstable_info.cache_helper)>>;
    requires StackLike<std::remove_reference_t<decltype(sstable_info.stack)>>;

    // Fields specifically for describing the current state of the current request.
    requires RequestBatchLike<std::remove_reference_t<decltype(*(sstable_info.req_batch))>>;
    requires std::same_as<bool, decltype(sstable_info.waiting_on_io)>;
    requires std::same_as<bool, decltype(sstable_info.already_waited)>;
    /* When not performing I/O for incoming requests, the desired_sstable_offset will 
    actually be a bitmask during initialization which will be a nonnegative number i 
    if the initialization reading is currently at i and is waiting for the two file 
    chunks from offset i to be read, -1 if the sstable's initializing reading is done 
    or if the sstable's parameters have been initialized and is 
    waiting for the sstable file handle to open, and -2 if the sstable's rings (and 
    other parameters as necessary) have not been initialized yet.
    */
    requires std::same_as<std::size_t, decltype(sstable_info.desired_sstable_offset)>;
};