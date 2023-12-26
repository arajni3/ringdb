#pragma once
#include <concepts>
#include <atomic>
#include "request_batch.hpp"

template<typename RequestBatchWaitQueue>
concept RequestBatchWaitQueueLike = requires(RequestBatchWaitQueue req_batch_wq) {
    /* Field that tells whether the buffer queue is accessed by only one thread 
    ever.
    */
    requires std::same_as<bool, decltype(req_batch_wq.guard.is_single_thread)>;
    // this and the previous field should be packed into a named struct named "guard"
    requires std::same_as<std::atomic_uchar, decltype(req_batch_wq.guard.atomic_guard)>;
    
    requires RequestBatchLike<std::remove_reference_t<decltype(
        *req_batch_wq.pop_front())>>;
};

template<typename RequestBatchWaitQueue, typename RequestBatch>
concept RequestBatchWaitQueuePushBack = requires(
    RequestBatchWaitQueue req_batch_wq, RequestBatch* req_batch
) {
    requires RequestBatchWaitQueueLike<RequestBatchWaitQueue>;
    requires RequestBatchLike<RequestBatch>;
    {req_batch_wq.push_back(req_batch)} -> std::same_as<void>;
};