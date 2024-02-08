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
    // these fields and the previous field should be packed into a named struct named "guard"
    requires std::same_as<std::atomic_uchar, decltype(req_batch_wq.guard.atomic_consumer_guard)>;
    requires std::same_as<std::atomic_uchar, decltype(req_batch_wq.guard.atomic_producer_guard)>;
    /* atomically records the size of the queue to indirectly synchronize the consumer and the 
    current producer so that the producer can tend to the end of the queue without being blocked 
    by the consumer if it does not have to be (i.e., if the queue size is greater than 1); the small 
    tradeoff is that, in case of contending with the consumer (which should be rare), an extra atomic 
    load operation, namely, of this size variable, will have been performed
    */
    requires std::same_as<std::atomic_uint, decltype(req_batch_wq.guard.size)>;
    
    /* If not single-threaded, must acquire the consumer guard before calling; in this case, for sake of 
    performance, the consumer guard should be freed within this function so that the old front pointer, 
    if this queue is implemented as a linked list and is not null, can be deleted after freeing up the 
    consumer guard to maximize performance without having to expose the node type and internal 
    implementation to the rest of the application.
    */ 
    requires RequestBatchLike<std::remove_reference_t<decltype(
        *req_batch_wq.pop_front())>>;
};

template<typename RequestBatchWaitQueue, typename RequestBatch>
concept RequestBatchWaitQueuePushBack = requires(
    RequestBatchWaitQueue req_batch_wq, RequestBatch* req_batch, bool could_contend_with_consumer
) {

    requires RequestBatchWaitQueueLike<RequestBatchWaitQueue>;
    requires RequestBatchLike<RequestBatch>;
    /* If not single-threaded, must acquire and release the producer guard before and after calling, 
    respectively. Try to enqueue to the queue in a lock-free manner. Returns true if successful and 
    false otherwise.
    */ 
    {req_batch_wq.try_push_back(req_batch, could_contend_with_consumer)} -> std::same_as<bool>;

    /* regular unconditional enqueue for a request batch that was not found in the next level 
    and as a helper method for the previous method
    */
    {req_batch_wq.standard_push_back(req_batch)} -> std::same_as<void>;

};