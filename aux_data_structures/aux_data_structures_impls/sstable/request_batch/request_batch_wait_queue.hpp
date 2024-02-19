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
        actually finished being modified; acquire-release semantics suffice because using acquire 
        together with release on a read-modify-write enables global synchronization of loads and 
        stores on all different variables except for the producer trylock release store, which 
        must be serialized externally
        */
        if (!guard.is_single_thread) [[unlikely]] {
            guard.size.fetch_add(1, std::memory_order_acq_rel);
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
            /* if loaded size was 0, then the list is possibly empty and by the argument in the 
            comment of the last conditional branch below, we can increment the size in a relaxed 
            manner, but we need to check if the list is actually empty; otherwise, if the loaded 
            size was 1, then the list is not empty, and the consumer below may be operating (it may 
            have started after the consumer guard load above was done), so we need a stronger order 
            (acquire-release) in this case
            */
            } else if (!size_var) [[unlikely]] {
                if (!guard.atomic_consumer_guard.load(std::memory_order_acquire)) [[unlikely]] {
                    return false;
                } else {
                    back = new Node(req_batch);
                    if (!front) {
                        front = back;
                    }
                    guard.size.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (size_var == 1) [[unlikely]] {
                if (!guard.atomic_consumer_guard.load(std::memory_order_acquire)) [[unlikely]] {
                    return false;
                } else {
                    back->next = new Node(req_batch);
                    /* acquire-release semantics are fine here because by definition of release 
                    semantics, the store in this read-modify-write (hence the whole read-modify-write) 
                    will not happen before the pointer modification above, and by definition of 
                    acquire semantics, the load in this read-modify-write (hence the whole 
                    read-modify-write) will not happen after the producer trylock is released
                    */
                    guard.size.fetch_add(1, std::memory_order_acq_rel);
                }
            } else [[unlikely]] {
                /* if producer reaches this branch, then the sequentially consistent size load above 
                occurred right after the consumer atomically fetch-and-subtracted the size below, so 
                the list is definitely empty and we can (atomically) increment the size in a relaxed 
                manner because having a smaller globally visible size will never cause simultaneous 
                critical path access but at most cause threads to examine and possibly exit and 
                reenter either the producer or consumer function since both the size load above and 
                the consumer fetch-and-subtract below are done with acquire semantics
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
        }
        return nullptr;
    }
};