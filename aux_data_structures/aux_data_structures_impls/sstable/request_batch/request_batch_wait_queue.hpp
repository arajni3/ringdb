#pragma once
#include <atomic>
#include "../../../aux_data_structures_concepts/sstable/request_batch/request_batch.hpp"

template<RequestBatchLike RequestBatch>
struct alignas(ALIGN_NO_FALSE_SHARING) RequestBatchWaitQueueNode {
    RequestBatch* req_batch;
    RequestBatchWaitQueueNode* next;

    // must set this so that next field does not point to an undefined location for pop front
    RequestBatchWaitQueueNode(RequestBatch* req_batch): req_batch{req_batch}, next{nullptr} {}
};

template<RequestBatchLike RequestBatch>
class RequestBatchWaitQueue {
    public:
    struct {
        alignas(ALIGN_NO_FALSE_SHARING) std::atomic_uchar atomic_consumer_guard{1};
        alignas(ALIGN_NO_FALSE_SHARING) std::atomic_uint size{0};
        alignas(ALIGN_NO_FALSE_SHARING) std::atomic_uchar atomic_producer_guard{1};
        bool is_single_thread;
    } guard;

    private:
    typedef RequestBatchWaitQueueNode<RequestBatch> Node;
    alignas(ALIGN_NO_FALSE_SHARING) Node* front = nullptr;
    alignas(ALIGN_NO_FALSE_SHARING) Node* back;

    public:

    void standard_push_back(RequestBatch* req_batch) {
        if (!front) {
            front = back = new Node(req_batch);
        } else {
            back = back->next = new Node(req_batch);
        }
        /* perform size change only after actually completing the queue modification so that consumer 
        thread does not operate on the otherwise-unsynchronized list nodes before the list nodes have 
        actually finished being modified; release semantics suffice here
        */
        if (!guard.is_single_thread) [[unlikely]] {
            guard.size.fetch_add(1, std::memory_order_release);
        }
    }

    bool try_push_back(RequestBatch* req_batch, bool could_contend_with_consumer) {
        /* all atomic loads here must have acquire semantics to be synchronized perfectly with the 
        consumer; acquire semantics are sufficient in the producer logic itself because future 
        atomic operations on other atomic variables happen in a dependent manner, not independently
        */
        if (guard.is_single_thread) [[likely]] {
            standard_push_back(req_batch);
        } else if (could_contend_with_consumer) [[unlikely]] {
            if (!guard.atomic_consumer_guard.load(std::memory_order_acquire)) [[unlikely]] {
                return false;
            } else {
                standard_push_back(req_batch);
            }
        } else [[likely]] {
            int size_var = guard.size.load(std::memory_order_acquire);
            if (size_var > 1) [[likely]] {
                back = back->next = new Node(req_batch);
                /* since loaded size was >= 2, the next consumption cannot contend with the current 
                producer, so we can use a relaxed ordering here
                */
                guard.size.fetch_add(1, std::memory_order_relaxed);
            /* if loaded size was 0, then the list is definitely empty (because the consumer releases 
            the consumer guard with release semantics and so the consumer's relaxed size increment, 
            if issued, would have occurred before the consumer guard was released), and by the 
            argument in the comment of the last conditional branch below, we must increment the size 
            with release semantics to be safe, or if we really want to stretch performance and rely 
            on in-practice consumer timing for safety, then we can stick with a relaxed ordering; 
            otherwise, if the loaded size was 1, then the list is not empty, and the consumer below 
            may be operating (it may have started after the consumer guard load above was done), so 
            we need release semantics anyway
            */
            } else if (!size_var) [[unlikely]] {
                if (!guard.atomic_consumer_guard.load(std::memory_order_acquire)) [[unlikely]] {
                    return false;
                } else {
                    front = back = new Node(req_batch);
                    guard.size.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (size_var == 1) [[unlikely]] {
                if (!guard.atomic_consumer_guard.load(std::memory_order_acquire)) [[unlikely]] {
                    return false;
                } else {
                    back->next = new Node(req_batch);
                    /* release semantics are fine here because by definition of release 
                    semantics, the store in this read-modify-write (hence the whole read-modify-write) 
                    will not happen before the pointer modification above
                    */
                    guard.size.fetch_add(1, std::memory_order_release);
                }
            } else [[unlikely]] {
                /* if producer reaches this branch, then the sequentially consistent size load above 
                occurred right after the consumer atomically fetch-and-subtracted the size below, so 
                the list is definitely empty, but, to be perfectly safe, we must increment the size 
                with release semantics so that the increment does not happen before the pointer
                modification here is done so that the consumer does not detect a positive size before
                the pointer modification is done; in actuality, though, the consumer thread will 
                take a long time to come back to try consuming from the queue again because the 
                sstable worker thread event loop is pretty long, and so using relaxed instead of 
                release prevents having to issue an extra store fence operation, which may slightly 
                improve performance, though, to generalize this queue algorithm, we would want to 
                use release semantics over relaxed
                */
                front = back = new Node(req_batch);
                guard.size.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return true;
    }

    RequestBatch* pop_front() {
        if (guard.is_single_thread) [[likely]] {
            if (front) {
                RequestBatch* req_batch = front->req_batch;
                Node* node = front->next;
                delete front;
                front = node;
                return req_batch;
            }
        /* atomic load here must have acquire semantics to be synchronized perfectly with the 
        producer; acquire semantics are sufficient in the consumer ogic itself because future 
        atomic operations on other atomic variables happen in a dependent manner, not independently
        */
        /* fetch_sub atomically subtracts from the atomic and returns the value previously held; if 
        size ends up being negative, then we messed up, but it's not a big deal because the consumer 
        shouldn't have anything to contend with the producer anyway, so we can add back the size 
        in a relaxed manner outside of the critical path since the producer loads the size above 
        with acquire semantics
        */
        } else if (guard.size.fetch_sub(1, std::memory_order_acq_rel) - 1 >= 0) {
            RequestBatch* req_batch = front->req_batch;
            Node* node = front->next, node2 = front;
            front = node;
            /* delete old front node outside of entire critical path to minimize time spent waiting 
            to update size and free up consumer guard and thereby maximize performance without 
            exposing internal implementation to the rest of the application
            */
            guard.atomic_consumer_guard.store(1, std::memory_order_release);
            delete node2;
            return req_batch;
        } else [[unlikely]] {
            guard.size.fetch_add(1, std::memory_order_relaxed);
            guard.atomic_consumer_guard.store(1, std::memory_order_release);
        }
        return nullptr;
    }
};