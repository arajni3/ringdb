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
        actually finished being modified
        */
        guard.size.fetch_add(1);
    }

    bool try_push_back(RequestBatch* req_batch, bool could_contend_with_consumer) {
        /* all atomic loads here must have acquire semantics to be synchronized perfectly with the 
        consumer, and in fact they must have sequential consistency because they all guard a critical 
        path
        */
        if (guard.is_single_thread) [[likely]] {
            standard_push_back(req_batch);
        } else if (could_contend_with_consumer) [[unlikely]] {
            if (!guard.atomic_consumer_guard.load()) [[unlikely]] {
                return false;
            }
            standard_push_back(req_batch);
        } else [[likely]] {
            int size_var = guard.size.load();
            if (size_var > 1) [[likely]] {
                back = back->next = new Node(req_batch);
            } else if (size_var == 1) [[unlikely]] {
                if (!guard.atomic_consumer_guard.load()) [[unlikely]] {
                    return false;
                }
                standard_push_back(req_batch);
            } else [[unlikely]] {
                front = back = new Node(req_batch);
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
        producer, and in fact it must have sequential consistency because it guards the critical 
        path
        */
        } else if (guard.size.load()) {
            RequestBatch* req_batch = front->req_batch;
            Node* node = front->next, node2 = front;
            front = node;
            /* perform size change only after actually completing the queue modification so that 
            producer thread does not operate on the otherwise-unsynchronized list nodes before the 
            list nodes have actually finished being modified
            */
            guard.size.fetch_sub(1);
            // delete old front node outside of size critical path to improve performance
            delete node2;
            return req_batch;
        }
        return nullptr;
    }
};