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
    Node* front = nullptr, *back;

    public:

    bool try_push_back(RequestBatch* req_batch, bool could_contend_head) {
        if (guard.is_single_thread) [[likely]] {
            if (!front) {
                front = back = new Node(req_batch);
            } else {
                back = back->next = new Node(req_batch);
            }
        } else if (could_contend_head) [[unlikely]] {
            if (guard.atomic_consumer_guard.load()) [[unlikely]] {
                return false;
            }
            back = back->next = new Node(req_batch);
        } else [[likely]] {
            int size_var = guard.size.load();
            if (size_var > 1) [[likely]] {
                back = back->next = new Node(req_batch);
            } else if (size_var == 1) [[unlikely]] {
                if (guard.atomic_consumer_guard.load()) [[unlikely]] {
                    return false;
                }
                back = back->next = new Node(req_batch);
            } else [[unlikely]] {
                front = back = new Node(req_batch;)
            }
            size.fetch_add(1);
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
            return nullptr;
        } else if (size.fetch_sub(1)) {
            RequestBatch* req_batch = front->req_batch;
            Node* node = front->next;
            delete front;
            front = node;
            return req_batch;
        }
        return nullptr;
    }
};