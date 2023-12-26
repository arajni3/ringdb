#pragma once
#include <atomic>

template<unsigned int length, unsigned int start_num_buffers>
struct alignas(ALIGN_NO_FALSE_SHARING) BufferQueue {
    private:
    unsigned int queue_front = 0, back = 0;

    public:
    unsigned int cur_num_buffers = start_num_buffers, num_new_buffers = 0;

    // must be public in order for compiler to not complain about associated concept constraint
    char* queue[length];

    struct alignas(ALIGN_NO_FALSE_SHARING) {
        std::atomic_uchar atomic_guard{1};
        bool is_single_thread;
    } guard;
    
    char* front() {
        return this->queue[queue_front];
    }

    void push_back(char* buffer) {
        this->back = (this->back + 1) % length;
        this->queue[this->back] = buffer;
        ++num_new_buffers;
    }

    char* pop_front() {
        --num_new_buffers;
        char* front_buffer = this->queue[queue_front];
        this->queue_front = (this->queue_front + 1) % length;
        return front_buffer;
    }

};