#pragma once
#include <concepts>
#include <atomic>

template<typename BufferQueue>
concept BufferQueueLike = requires(BufferQueue buffer_queue, char* buffer) {
    // Field that tells whether the buffer queue is accessed by only one thread ever.
    requires std::same_as<bool, decltype(buffer_queue.guard.is_single_thread)>;
    // this and the previous field should be in a named struct "guard"
    requires std::same_as<std::atomic_uchar, decltype(buffer_queue.guard.atomic_guard)>;

    requires std::same_as<char*, std::remove_reference_t<decltype(buffer_queue.queue[0])>>;
    requires std::is_array_v<decltype(buffer_queue.queue)>;

    requires std::same_as<unsigned int, decltype(buffer_queue.cur_num_buffers)>;
    requires std::same_as<unsigned int, decltype(buffer_queue.num_new_buffers)>;
    {buffer_queue.front()} -> std::same_as<char*>;
    {buffer_queue.pop_front()} -> std::same_as<char*>;
    {buffer_queue.push_back(buffer)} -> std::same_as<void>;
};