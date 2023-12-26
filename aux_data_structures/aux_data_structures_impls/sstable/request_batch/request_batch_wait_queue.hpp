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
    struct alignas(ALIGN_NO_FALSE_SHARING) {
        std::atomic_uchar atomic_guard{1};
        bool is_single_thread;
    } guard;

    private:
    typedef RequestBatchWaitQueueNode<RequestBatch> Node;
    Node* front = nullptr, *back;

    public:

    void push_back(RequestBatch* req_batch) {
        if (!front) {
            front = new Node(req_batch);
            back = front;
        } else {
            back->next = new Node(req_batch);
            back = back->next;
        }
    }

    RequestBatch* pop_front() {
        if (front) {
            RequestBatch* req_batch = front->req_batch;
            Node* node = front->next;
            delete front;
            front = node;
            return req_batch;
        }
        return nullptr;
    }
};