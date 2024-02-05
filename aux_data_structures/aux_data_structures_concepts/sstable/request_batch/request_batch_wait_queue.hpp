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
    // these two fields and the previous field should be packed into a named struct named "guard"
    requires std::same_as<std::atomic_uchar, decltype(req_batch_wq.guard.atomic_consumer_guard)>;
    requires std::same_as<std::atomic_uchar, decltype(req_batch_wq.guard.atomic_producer_guard)>;
    
    /* If not single-threaded, must acquire and release the consumer guard before and after calling, 
    respectively
    */ 
    requires RequestBatchLike<std::remove_reference_t<decltype(
        *req_batch_wq.pop_front())>>;
};

template<typename RequestBatchWaitQueue, typename RequestBatch>
concept RequestBatchWaitQueuePushBack = requires(
    RequestBatchWaitQueue req_batch_wq, RequestBatch* req_batch, bool could_contend_head
) {

    requires RequestBatchWaitQueueLike<RequestBatchWaitQueue>;
    requires RequestBatchLike<RequestBatch>;
    /* If not single-threaded, must acquire and release the producer guard before and after calling, 
    respectively. Try to enqueue to the queue in a lock-free manner. Returns true if successful and 
    false otherwise.
    */ 
    {req_batch_wq.try_push_back(req_batch, could_contend_head)} -> std::same_as<bool>;

    // regular unconditional enqueue for a request batch that was not found in the next level
    {req_batch_wq.not_found_push_back(req_batch)} -> std::same_as<void>;

};