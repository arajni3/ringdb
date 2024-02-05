#define _FILE_OFFSET_BITS 64
/* use the above macro to allow file sizes to be expressed by 64-bit numbers; it must 
be included before any include statement
*/

#include <concepts>
#include <algorithm>
#include <thread>

/* use for std::launder for pointer-number-pointer conversions object lifetimes
*/
#include <new>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>

// use std::memcmp to compare N-byte char buffers without stopping at a '\0'
#include <cstring>

// use to convert __u64 to uintptr_t and then to the desired pointer type wi
#include <cstdint>

// use for snprintf
#include <climits>
#include <cstdio>

#include <cmath>
#include <numeric>
#include <liburing.h>

#include "aux_data_structures/aux_data_structures_concepts/level_zero/memtable.hpp"

#include "aux_data_structures/aux_data_structures_concepts/level_info/level_info.hpp"
#include "aux_data_structures/aux_data_structures_concepts/level_info/buffer_queue/buffer_queue.hpp"
#include "aux_data_structures/aux_data_structures_concepts/level_info/filter/filter.hpp"
#include "aux_data_structures/aux_data_structures_concepts/level_info/sparse_index/sparse_index.hpp"
#include "aux_data_structures/aux_data_structures_concepts/level_info/decomposition/decomposition.hpp"

#include "aux_data_structures/aux_data_structures_concepts/sstable/sstable_info.hpp"
#include "aux_data_structures/aux_data_structures_concepts/sstable/sstable_cache_helper/sstable_cache_helper.hpp"
#include "aux_data_structures/aux_data_structures_concepts/sstable/request_batch/request_batch.hpp"
#include "aux_data_structures/aux_data_structures_concepts/sstable/request_batch/request_batch_wait_queue.hpp"
#include "aux_data_structures/aux_data_structures_concepts/sstable/request_batch/request_segment.hpp"
#include "aux_data_structures/aux_data_structures_concepts/sstable/sstable_cache_helper/cache_buffer_entry/cache_buffer_entry.hpp"
#include "aux_data_structures/aux_data_structures_concepts/sstable/stack/stack.hpp"

#include "aux_data_structures/aux_data_structures_concepts/base_request/base_request.hpp"

#include "aux_data_structures/aux_data_structures_concepts/connection_pool/connection_pool.hpp"
#include "aux_data_structures/aux_data_structures_concepts/connection_pool/connection_request/connection_request.hpp"
#include "aux_data_structures/aux_data_structures_concepts/connection_pool/readwrite_pool/readwrite_pool.hpp"

#include "aux_data_structures/enums/request_type.hpp"

#include "constinit_constants.hpp"

#ifdef RINGDB_TEST
#include "test_ringdb.hpp"
#endif

#define _GNU_SOURCE

/* Number of sstables that a worker thread must handle.
*/
#define WORKER_SSTABLE_BATCH_SIZE  (NUM_SSTABLES / NUM_SSTABLE_WORKER_THREADS)

// Includes the sstables as well as the server socket fd.
#define NUM_FILE_FDS (1 + NUM_SSTABLES)

#define NUM_SSTABLE_LEVELS (NUM_SSTABLES / LEVEL_FACTOR)

// Can use huge pages in large-scale settings for better performance.
#define NUM_BATCHES (MAX_SIMULTANEOUS_CONNECTIONS / PAGE_SIZE)

// Includes the sstables as well as the server socket fd.
#define NUM_FILE_FDS (1 + NUM_SSTABLES)

/* When using conditional store in some kind of loop, use weak CAS instead of strong 
because it's faster than strong CAS on non-CISC architectures, which use LL/SC instead
of actual CAS (on CISC architectures like x86, weak CAS will be the same as normal 
strong CAS) and LL/SC will eventually converge to a successful operation if it is used
repeatedly in a loop like this one.
*/

/* Because this database is meant to be a large-scale database, we care more about 
maximizing throughput than minimizing latency, and so we will try to optimize the hot 
path (fast path), which is the uncontended branch, at the possible cost of deoptimizing 
the cold path (slow path), which is the contended branch.
*/

template<
MemTableLike MemTable,

LevelInfoLike LevelInfo,
BufferQueueLike BufferQueue,
FilterLike Filter,
SparseIndexLike SparseIndex,
DecompositionLike Decomposition,

SSTableInfoLike SSTableInfo,
SSTableCacheHelperLike SSTableCacheHelper,
CacheLocationLike CacheLocation,
RequestBatchLike RequestBatch,
RequestBatchWaitQueueLike RequestBatchWaitQueue,
RequestSegmentLike RequestSegment,
CacheBufferEntryLike CacheBufferEntry,
StackLike Stack,

BaseRequestLike BaseRequest,

ConnectionPoolLike ConnectionPool,
ConnectionRequestLike ConnectionRequest,
ReadWritePoolLike ReadWritePool
>
requires ConnectionRequestReq<ConnectionRequest, BaseRequest> 
&& RequestBatchReq<RequestBatch, BaseRequest>
&& LevelInfoDecompose<LevelInfo, RequestBatch>
&& RequestBatchWaitQueuePushBack<RequestBatchWaitQueue, RequestBatch>
&& MemTableWrite<MemTable, SparseIndex>
class LSMTree {
    public:        
    void initialize(auto sstable_callback, auto network_callback) {
        #ifndef RINGDB_TEST
        this->sstable_dir_fd = open("/sstable", O_CREAT, mode);
        #else
        this->sstable_dir_fd = open("/sstab1e", O_CREAT, mode);
        #endif
        this->sstable_page_cache_buffers = (char*)mmap(
            nullptr,
            fair_unaligned_sstable_page_cache_buffer_size * NUM_SSTABLES,
            PROT_READ | PROT_WRITE,
            /* MAP_HUGETLB uses the default huge page size on the host platform, so 
            make sure to change that if applicable
            */
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED 
            | (PAGE_SIZE > 4096) ? MAP_HUGETLB : 0,
            -1, 
            0
        );

        this->level_infos = new LevelInfo[NUM_SSTABLE_LEVELS];
        mlock(this->level_infos, sizeof(LevelInfo) * LEVEL_FACTOR);
        /* level info as well as buffer queue are accessed by at most one thread ever if and
        only if the level does not lie on the boundary between any two sstable worker thread 
        batches because a worker thread can access its last level (whose last sstable 
        will be part of the next worker thread's batch), the level after that (which will 
        also be part of the next worker thread's batch), and the level before its first level, 
        which is part of the previous worker thread's batch
        */
        this->level_infos[0].set_is_single_thread(true);
        for (unsigned int j = 0; j < LEVEL_FACTOR; ++j) {
            this->level_infos[0].sstable_infos[j].req_batch_wq.guard.is_single_thread = true;
        }
        unsigned int location;
        bool not_beginning, not_end;
        for (unsigned int level = 1; level < NUM_SSTABLE_LEVELS - 1; ++level) {
            location = level % (WORKER_SSTABLE_BATCH_SIZE / LEVEL_FACTOR);
            not_beginning = location > 0;
            not_end = location < LEVEL_FACTOR - 1;
            this->level_infos[level].set_is_single_thread(not_beginning && not_end);
            /* request batch wait queue is accessed by the previous level but not by the next 
            level, and for each level in-between, the last sstable on a shared level will be 
            accessed by both the current worker thread and the next worker thread while the 
            other sstables on the level will be accessed only by the former
            */
            for (unsigned int j = 0; j < LEVEL_FACTOR - 1; ++j) {
                this->level_infos[level].sstable_infos[j].req_batch_wq.guard.is_single_thread = 
                not_beginning;
            }
            this->level_infos[level].sstable_infos[LEVEL_FACTOR - 1].req_batch_wq
            .guard.is_single_thread = not_end;
        }
        this->level_infos[NUM_SSTABLE_LEVELS - 1].set_is_single_thread(true);
        for (unsigned int j = 0; j < LEVEL_FACTOR; ++j) {
            this->level_infos[NUM_SSTABLE_LEVELS - 1].sstable_infos[j].req_batch_wq
            .guard.is_single_thread = true;
        }

        cpu_set_t main_thread_cpu;
        CPU_ZERO(&main_thread_cpu);
        CPU_SET(0, &main_thread_cpu);
        sched_setaffinity(0, sizeof(cpu_set_t), &main_thread_cpu);

        this->memtables = new MemTable[LEVEL_FACTOR];
        mlock(this->memtables, sizeof(MemTable) * LEVEL_FACTOR);

        io_uring_params main_thread_params;
        memset(&main_thread_params, 0, sizeof(io_uring_params));
        main_thread_params.sq_thread_cpu = 2;
        main_thread_params.flags = IORING_SETUP_SQPOLL 
        | IORING_SETUP_SUBMIT_ALL
        | IORING_SETUP_COOP_TASKRUN
        | IORING_SETUP_TASKRUN_FLAG
        | IORING_SETUP_SINGLE_ISSUER
        | IORING_SETUP_DEFER_TASKRUN
        | IORING_SETUP_SQ_AFF;
        main_thread_params.features = IORING_FEAT_NATIVE_WORKERS;
        
        io_uring_queue_init_params(NUM_BATCHES, this->main_thread_comm_ring, 
        &main_thread_params);
        io_uring_register_ring_fd(this->main_thread_comm_ring);

        unsigned int i;
        for (i = 0; i < NUM_SSTABLE_WORKER_THREADS; ++i) {
            std::thread t(sstable_callback, this, i);
            t.detach();
        }

        while (this->num_read.load() < NUM_SSTABLES);
        this->start_ringdb(network_callback);     
    }

    private:
    /* The file offset must be a multiple of the block size in order for direct I/O
    to work. Since we will be working with array-based trees, a node at index i has 
    its children at indices 2i + 1 and 2i + 2. Even if i is a multiple of the block 
    size, neither 2i + 1 nor 2i + 2 is since the block size will a power of 2 greater
    than 2. Hence, each index k must be rounded to the largest multiple of the block 
    size less than or equal to it in order for a file chunk to include it; this 
    multiple is simply k - (k mod BLOCK_SIZE), which is the same as BLOCK_SIZE * 
    floor(k / BLOCK_SIZE), but the former is faster since it has a subtraction 
    instead of a multiplication, and it has no divisions because reducing modulo 2**p 
    is just zeroing out all but the lower p bits. Since each file offset k will be an
    size_t int, k mod BLOCK_SIZE is simply k & ((1UL << 63) - (1UL << (p - 1))), 
    where p is the power-of-2 exponent of BLOCK_SIZE.
    */
    inline size_t round_to_block_size_multiple(size_t i) {
        return i - (i & ((1UL << 63) - 
        (1UL << (my_log2_floor<static_cast<double>(BLOCK_SIZE)>() - 1))));
    }

    inline void schedule_initialization_read(SSTableInfo* sstable_info, 
    struct io_uring* scheduler_ring, unsigned int sstable_num, struct io_uring_sqe* sqe) {
        sqe = io_uring_get_sqe(sstable_info->io_ring);
        io_uring_prep_read(sqe, sstable_num, nullptr, 
        fair_aligned_sstable_page_cache_buffer_size, 
        sstable_info->desired_sstable_offset);
        io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_BUFFER_SELECT | 
        IOSQE_IO_LINK);

        sqe = io_uring_get_sqe(sstable_info->io_ring);
        io_uring_prep_read(sqe, sstable_num, nullptr, 
        fair_aligned_sstable_page_cache_buffer_size, 
        sstable_info->desired_sstable_offset + 
        fair_aligned_sstable_page_cache_buffer_size);
        io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_BUFFER_SELECT |
        IOSQE_IO_LINK);

        sqe = io_uring_get_sqe(sstable_info->io_ring);
        io_uring_prep_msg_ring(sqe, scheduler_ring->ring_fd, 
        0, static_cast<__u64>(sstable_num), 0);
        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);

        io_uring_submit(sstable_info->io_ring);
    }

    /* push the current node's non-null children to the top of the stack
    with the left child being at the very top
    */ 
    inline void move_down_to_children(SSTableInfo* sstable_info, char* null_key) {
        auto cur_request_info = sstable_info->stack.top();
        unsigned int cur_request = cur_request_info[0], left = cur_request_info[1], 
        right = cur_request_info[2];
        sstable_info->stack.pop_top();
        unsigned int left_child = (left + cur_request) >> 1;
        unsigned int right_child = (cur_request + right) >> 1;

        // if the the current request node has a right child, push it onto the stack
        if (right_child > cur_request && std::memcmp(sstable_info
        ->req_batch->content.readwrite_pool.data[right_child].buffer + 5, null_key, KEY_LENGTH)) {
            sstable_info->stack.push_top(sstable_info->desired_sstable_offset, right_child, 
            cur_request, right);
        }

        // if the current request node has a left child, push it onto the stack
        if (left_child < cur_request && std::memcmp(sstable_info->req_batch->content.readwrite_pool
        .data[left_child].buffer + 5, null_key, KEY_LENGTH)) {
            sstable_info->stack.push_top(sstable_info->desired_sstable_offset, left_child, left, 
            cur_request);
        }
    }

    /* Traverse the sorted read request batch array in a preorder manner (by induction, 
    every sorted array represents a balanced binary search tree with the root being the 
    middle element, the left child being the lower quarter element, and the right child 
    being the upper quarter element) so that the requests with the smallest keys get 
    completed first to keep the smallest (highest) sstable keys in the page cache as much 
    as possible because those keys will be the most accessed, especially on reads. Pop the 
    current request off the stack only when it is finished, not at the beginning. Also, save 
    the resulting desired sstable offset when the current request is finished; because the 
    SSTable is immutable, the final offset of the current request can be cached and used 
    as the starting point for its child requests, which significantly reduces the number of 
    read operations required for future requests.
    */
    inline void try_sstable_tree_read_in_memory(SSTableInfo* sstable_info, 
    struct io_uring* scheduler_ring, ConnectionRequest* conn_req, CacheLocation location, 
    CacheBufferEntry* buffer_in_cache, unsigned int sstable_num, unsigned int level, 
    size_t left_boundary, char* null_key, char* tombstone_value, LevelInfo* 
    level_infos, unsigned int index_in_level) {
        unsigned int cur_request;
        do {
            if (sstable_info->desired_sstable_offset >= sstable_info
            ->cache_helper.get_cur_min_invalid_offset()) {
                /* the desired sstable offset is beyond the size of the sstable, so it is not 
                in the sstable, so we should end the current request and move on to the next 
                ones
                */
                this->move_down_to_children(sstable_info, null_key);
            } else {
                auto stack_top = sstable_info->stack.top();
                sstable_info->desired_sstable_offset = stack_top[0];
                cur_request = stack_top[1];
                conn_req = sstable_info->req_batch->content.readwrite_pool.data + cur_request;
                location = sstable_info->cache_helper.get_buffer_info(sstable_info->
                desired_sstable_offset);
                if (location.sstable_offset_boundary[0] == -1) {
                    // offset not found, must read from storage
                    if (!sstable_info->cache_helper.free_buffers_left) {
                        io_uring_buf_ring_advance(sstable_info->buffer_ring, 1);
                        sstable_info->cache_helper
                        .replenish_least_recently_selected_buffer();
                    }
                    struct io_uring_sqe* sqe = io_uring_get_sqe(sstable_info->io_ring);
                    left_boundary = this->round_to_block_size_multiple(
                        sstable_info->desired_sstable_offset);
                    io_uring_prep_read(sqe, sstable_num, nullptr,
                    fair_aligned_sstable_page_cache_buffer_size, left_boundary);
                    io_uring_sqe_set_data64(sqe, left_boundary);
                    io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_BUFFER_SELECT | 
                    IOSQE_IO_LINK);
                    if (sstable_info->cache_helper.free_buffers_left == 2) {
                        /* multiple of block size times 2 is a multiple of the block size
                        */
                        left_boundary = left_boundary ? BLOCK_SIZE : left_boundary << 1;
                        sqe = io_uring_get_sqe(sstable_info->io_ring);
                        io_uring_prep_read(sqe, sstable_num, nullptr,
                        fair_aligned_sstable_page_cache_buffer_size, left_boundary);
                        io_uring_sqe_set_data64(sqe, left_boundary);
                        io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_BUFFER_SELECT
                        | IOSQE_IO_LINK);
                    }
                    sqe = io_uring_get_sqe(sstable_info->io_ring);
                    io_uring_prep_msg_ring(sqe, scheduler_ring->ring_fd, 0, 
                    static_cast<__u64>(sstable_num), 0);
                    io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
                    io_uring_submit(sstable_info->io_ring);
                    sstable_info->waiting_on_io = true;
                } else {
                    // search page cache buffer for offset
                    unsigned int num_entries = (location.sstable_offset_boundary[1] - 
                    location.sstable_offset_boundary[0]) / sizeof(CacheBufferEntry);
                    buffer_in_cache = std::launder(reinterpret_cast<CacheBufferEntry*>(
                        sstable_info->page_cache_buffers[location.buffer_id]));
                    int res, not_null;
                    unsigned int index_in_buffer = sstable_info->desired_sstable_offset
                    - location.sstable_offset_boundary[0];
                    while (index_in_buffer < num_entries && (not_null = std::memcmp(
                        buffer_in_cache[index_in_buffer].key, null_key, KEY_LENGTH)) && 
                        (res = std::memcmp(buffer_in_cache[index_in_buffer].key, 
                        conn_req->buffer + 5, KEY_LENGTH))) {
                            /* if current sstable key is greater than request key, then 
                            res > 0 and we should go to the left child hence subtract 1 
                            from 2 here, otherwise res < 0 and we should go to the right 
                            child hence not subtract anything from 2 here
                            */
                            index_in_buffer = (index_in_buffer << 1) + 2 - (res > 0);
                        }
                        /* If not found yet, continue searching down the sstable, otherwise
                        we've found the key. The not_null variable would be 0 at the 
                        index_in_buffer index value if there was no entry there and hence 
                        that the key could be inserted there. Note that the request keys 
                        will never be null, i.e., all null-terminating characters.
                        */
                        if (res)  {
                            sstable_info->desired_sstable_offset = index_in_buffer +
                            location.sstable_offset_boundary[1];
                        } else if (not_null) {
                            memcpy(conn_req->buffer + 5 + KEY_LENGTH, 
                            buffer_in_cache[index_in_buffer].value,
                            VALUE_LENGTH);
                            this->move_down_to_children(sstable_info, null_key);
                        } else {
                            // location of key is empty (it would be inserted here)
                            this->move_down_to_children(sstable_info, null_key);
                        }
                }
            }
        } while (!(sstable_info->stack.empty() || sstable_info->waiting_on_io));

        /* move requests to the next level if all were finished in the 
        current sstable
        */
        if (sstable_info->stack.empty() && level < NUM_SSTABLE_LEVELS - 1) {
            sstable_info->waiting_on_io = false;
            this->insert_into_wqs(level + 1, level_infos, 
            sstable_info->req_batch, sstable_info, index_in_level);
        }
    }


    public:
    void sstable_worker_thread(int worker_thread_num) {
        LevelInfo* level_infos = this->level_infos;
        int first_sstable_num = worker_thread_num * WORKER_SSTABLE_BATCH_SIZE;
        int second_sstable_num = first_sstable_num + WORKER_SSTABLE_BATCH_SIZE - 1;

        pthread_t sstable_worker_thread = pthread_self();
        cpu_set_t sstable_worker_thread_cpu;
        CPU_ZERO(&sstable_worker_thread_cpu);
        /* balance thread allocation among last (NUM_PROCESSORS - 3) CPU cores as 
        evenly as possible by using ring-like modular arithmetic similar to consistent 
        hashing
        */
        CPU_SET(3 + (worker_thread_num % (NUM_PROCESSORS - 3)), 
        &sstable_worker_thread_cpu);
        pthread_setaffinity_np(
            sstable_worker_thread, 
            sizeof(cpu_set_t), 
            &sstable_worker_thread_cpu
        );

        /* Initialize scheduler ring. We will use the io_uring_prep_msg_ring function 
        along with IOSQE_IO_LINK to link each asynchronous I/O batch operation of an 
        sstable in the worker thread's batch to a message in the worker thread's io 
        ring so that the message will appear after the batch I/O operation is 
        finished. While cycling through each sstable in the worker thread's batch, we 
        will peek into the CQ ring of the worker thread's io ring in a nonblocking, 
        syscall-free manner to check for messages  and identify the sstable 
        corresponding to each message by checking the user_data field of each message. 
        This will enable fair scheduling of the sstables' I/O operations in userspace 
        and provide minimal latency as the message sending will be tied to actual I/O 
        completions in the kernel as opposed to naive heuristics in userspace. For 
        this same reason, the sstable I/O rings should also not have the coop taskrun 
        and taskrun flag flags and should use peek cqe to get the CQE entries safely.
        */
        struct io_uring_params ring_params;
        memset(&ring_params, 0, sizeof(io_uring_params));
        /* do not use the coop taskrun or taskrun flag flags because they will 
        cause the peek cqe to be a syscall and thereby 
        prevent being able to poll the completion queue in a fast, syscall-free 
        manner
        */
        ring_params.flags =  IORING_SETUP_SQPOLL
        | IORING_SETUP_SINGLE_ISSUER
        | IORING_SETUP_CQSIZE
        | IORING_SETUP_DEFER_TASKRUN 
        | IORING_SETUP_SQ_AFF;
        ring_params.features = IORING_FEAT_NATIVE_WORKERS;

        // put all sq poll threads on CPU 2
        ring_params.sq_thread_cpu = 2;

        /* allow receiving one linked message for each asynchronous I/O batch 
        operation from each sstable in the worker thread's batch 
        */
        ring_params.cq_entries = WORKER_SSTABLE_BATCH_SIZE;

        struct io_uring* scheduler_ring;
        io_uring_queue_init_params(2, scheduler_ring, &ring_params);
        io_uring_register_ring_fd(scheduler_ring);

        unsigned int num_initialized_read = 0;
        /* First initialize the necessary sstable info fields. To save space, for each 
        sstable, use the is_flushed_to field in the sstable_info data structure to 
        tell if reading was finished. We will also use the desired sstable 
        offset field as a bitmask when not doing initialization reading. The other 
        sstable info fields, as well as the desired sstable offset field as a bitmask
        should be initialized as necessary in the constructor.
        */
        char* tombstone_value = new char[VALUE_LENGTH];
        std::fill(tombstone_value, tombstone_value + VALUE_LENGTH, '\0');
        char* null_key = new char[KEY_LENGTH];
        std::fill(null_key, null_key + KEY_LENGTH, '\0');
        SSTableInfo* sstable_info;
        struct io_uring_sqe* sqe;
        struct io_uring_cqe* cqes[2] = {nullptr, nullptr};
        unsigned int level, index_in_level, sstable_num, i, j, k, m, num_entries;
        int min_cmp, max_cmp;
        CacheBufferEntry* buffer_in_cache;
        while (num_initialized_read < WORKER_SSTABLE_BATCH_SIZE) {
            for (i = first_sstable_num; i <= second_sstable_num; ++i) {
                io_uring_peek_cqe(scheduler_ring, cqes);
                if (cqes[0]) {
                    sstable_num = static_cast<unsigned int>(cqes[0]->user_data);
                    j = 0;
                    level = sstable_num / LEVEL_FACTOR;
                    index_in_level = sstable_num - (level * LEVEL_FACTOR);
                    sstable_info = &(level_infos[level].sstable_infos[index_in_level]);
                    io_uring_cqe_seen(scheduler_ring, cqes[0]);

                    if (io_uring_peek_batch_cqe(sstable_info->io_ring, cqes, 2)
                    == 2) { // it's a file read
                        for (j = 0; j < 2; ++j) {
                            if (cqes[j]->res > 0) {
                                sstable_info->desired_sstable_offset += cqes[j]
                                ->res;
                                sstable_info->is_flushed_to = true;
                                num_entries = cqes[j]->res / sizeof(CacheBufferEntry);
                                buffer_in_cache = std::launder(reinterpret_cast<CacheBufferEntry*>
                                (sstable_info->page_cache_buffers[
                                    cqes[j]->flags >> IORING_CQE_BUFFER_SHIFT]));
                                for (k = 0; k < num_entries; ++k) {
                                    if (std::memcmp(buffer_in_cache[i].value, tombstone_value, 
                                    VALUE_LENGTH)) {
                                        min_cmp = std::memcmp(buffer_in_cache[i].key, 
                                        sstable_info->min_key, KEY_LENGTH);
                                        max_cmp = std::memcmp(buffer_in_cache[i].key, 
                                        sstable_info->max_key, 
                                        KEY_LENGTH);
                                        if (min_cmp < 0) {
                                            memcpy(buffer_in_cache[i].key, 
                                            sstable_info->min_key, KEY_LENGTH);
                                        }
                                        if (max_cmp > 0) {
                                            memcpy(buffer_in_cache[i].key, 
                                            sstable_info->max_key, KEY_LENGTH);
                                        }
                                        level_infos[level].filters[index_in_level]
                                        .insert_key(buffer_in_cache[i].key);
                                    }
                                }
                            }
                            if (cqes[j]->res < 
                            fair_aligned_sstable_page_cache_buffer_size) {
                                sstable_info->desired_sstable_offset = -1;
                                level_infos[level].sparse_index.insert_range(
                                    sstable_info->min_key,
                                    sstable_info->max_key, 
                                    index_in_level
                                );
                                /* Don't need to slow down instruction pipeline with 
                                sequential consistency or even acquire/release 
                                semantics (though acquire/release semantics occur by 
                                default on x86) because the only reader is the main 
                                thread, which reads in a loop with sequential 
                                consistency and hence will never start the network 
                                thread prematurely (i.e., before all sstable 
                                initializations have completed), so strict ordering 
                                with respect to this variable is not necessary, and 
                                sequential consistency is not needed because this 
                                variable does not guard a critical section.
                                */
                                this->num_read.fetch_add(1, std::memory_order_relaxed);
                                sstable_info->waiting_on_io = false;
                            } else if (j == 1) {
                                this->schedule_initialization_read(sstable_info, 
                                scheduler_ring, sstable_num, sqe);
                            }
                        }
                        io_uring_buf_ring_cq_advance(sstable_info->io_ring, 
                        sstable_info->buffer_ring, 2);
                    } else { // file descriptor of sstable has been opened
                        this->fixed_file_fds[sstable_num] = static_cast<int>(cqes[0]
                        ->user_data);
                        io_uring_register_files(sstable_info->io_ring, 
                        this->fixed_file_fds + sstable_num, 1);
                        sstable_info->desired_sstable_offset = 0;
                        this->schedule_initialization_read(sstable_info, scheduler_ring, 
                        sstable_num, sqe);
                    }

                }

                sstable_num = i;
                level = i / LEVEL_FACTOR;
                index_in_level = i - (level * LEVEL_FACTOR);
                sstable_info = &(level_infos[level].sstable_infos[index_in_level]);

                if (sstable_info->desired_sstable_offset == -2) {
                    // initialize necessary parameters
                    memset(&ring_params, 0, sizeof(io_uring_params));
                    ring_params.flags = IORING_SETUP_SINGLE_ISSUER
                    | IORING_SETUP_ATTACH_WQ
                    | IORING_SETUP_CQSIZE
                    | IORING_SETUP_DEFER_TASKRUN;
                    ring_params.wq_fd = scheduler_ring->ring_fd;
                    io_uring_queue_init_params(3, sstable_info->io_ring, 
                    &ring_params);
                    io_uring_register_ring_fd(sstable_info->io_ring);

                    std::fill(sstable_info->min_key, sstable_info->min_key + 
                    KEY_LENGTH, 
                    (char)255);
                    std::fill(sstable_info->max_key, sstable_info->max_key + 
                    KEY_LENGTH, 
                    '\0');


                    // make open() request
                    // sstable file name format will be `sstable/${level}/${index_in_level}`
                    // or replace sstable with sstab1e when testing
                    #ifndef RINGDB_TEST
                    memcpy(sstable_info, "/sstable", 8);
                    #else
                    memcpy(sstable_info, "/sstab1e", 8);
                    #endif
                    int dir_len = snprintf(sstable_info->file_path + 8, level_str_len, 
                    "%u", level);
                    sstable_info->file_path[dir_len] = '/';
                    snprintf(sstable_info->file_path + dir_len + 1, sstable_number_str_len, 
                    "%u", index_in_level);

                    sqe = io_uring_get_sqe(sstable_info->io_ring);
                    io_uring_prep_openat(sqe, this->sstable_dir_fd, 
                    sstable_info->file_path, O_CREAT | O_DIRECT, mode);
                    io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);

                    sqe = io_uring_get_sqe(sstable_info->io_ring);
                    io_uring_prep_msg_ring(sqe, scheduler_ring->ring_fd, 0, 
                    static_cast<__u64>(i), 0);
                    io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);

                    io_uring_submit(sstable_info->io_ring);
                    sstable_info->desired_sstable_offset = 0;

                    // initialize sstable buffer ring
                    /* pointer to simulate a buffer ring in the application code itself
                    because liburing does not currently provide a way to remove 
                    buffers from a buffer ring without unregistering and reregistering 
                    the whole ring; it does for regular provided buffers but that is 
                    otherwise less efficient than a buffer ring
                    */
                    sstable_info->buffer_ring_id = sstable_num;
                    sstable_info->insert_buffers_from = max_sstable_height;
                    for (m = 0; m < max_sstable_height; ++m) {
                        sstable_info->page_cache_buffers[m] = 
                        this->sstable_page_cache_buffers + 
                        fair_unaligned_sstable_page_cache_buffer_size * (
                        sstable_num + m);
                    }

                    int error_val;
                    /* enable transferring of other sstables' page cache buffer to this
                    sstable to utilize page cache more actively and efficiently
                    */
                    sstable_info->buffer_ring = io_uring_setup_buf_ring(
                    sstable_info->io_ring, max_buffer_ring_size, sstable_num,
                    0, &error_val);
                    for (m = 0; i < max_sstable_height; ++m) {
                        io_uring_buf_ring_add(sstable_info->buffer_ring, 
                        sstable_info->page_cache_buffers[m], 
                        fair_aligned_sstable_page_cache_buffer_size,
                        m, 
                        io_uring_buf_ring_mask(max_buffer_ring_size), 
                        m);
                        sstable_info->cache_helper.add_buffer();
                    }  
                    io_uring_buf_ring_advance(sstable_info->buffer_ring, 
                    max_sstable_height);
                }
            }
        }

        // do the main sstable request I/O logic
        unsigned int num_chunks_queued, num_new_buffers;
        unsigned char my_zero = 0;
        BufferQueue* buffer_queue, *prev_buffer_queue;
        unsigned int prev_sstable_num, prev_level, prev_index_in_level;
        char* new_buffers[max_sstable_height];
        ConnectionRequest* conn_req; 
        CacheLocation location; 
        size_t left_boundary;

        /* first get network ring fd and copy it locally to avoid false sharing and cache 
        misses
        */
        int network_ringfd = -1;
        while (network_ringfd == -1) {
            io_uring_peek_cqe(scheduler_ring, cqes);
            if (cqes[0]) {
                network_ringfd = static_cast<int>(cqes[0]->user_data);
            }
        }

        while (true) {
            for (i = first_sstable_num; i <= second_sstable_num; ++i) {
                io_uring_peek_cqe(scheduler_ring, cqes);
                if (cqes[0]) {
                    sstable_num = static_cast<unsigned int>(cqes[0]->user_data);
                    j = 0;
                    level = sstable_num / LEVEL_FACTOR;
                    index_in_level = sstable_num - (level * LEVEL_FACTOR);
                    sstable_info = &(level_infos[level].sstable_infos[index_in_level]);
                    io_uring_cqe_seen(scheduler_ring, cqes[0]);

                    num_chunks_queued = io_uring_peek_batch_cqe(
                        sstable_info->io_ring, cqes, 2);
                    // zero cqes in sstable completion queue means flush was completed
                    if (!num_chunks_queued) {
                        sstable_info->is_flushed_to = true;
                        this->is_practically_full = level == NUM_SSTABLE_LEVELS - 1;
                    } else {
                        for (j = 0; j < num_chunks_queued; ++j) {
                            unsigned int buffer_id = cqes[j]->flags 
                            >> IORING_CQE_BUFFER_SHIFT;
                            left_boundary = static_cast<size_t>(
                                io_uring_cqe_get_data64(cqes[j]));
                            size_t right_boundary = left_boundary + cqes[j]->res;
                            if (cqes[j]->res > 0) {
                                sstable_info->cache_helper.
                                map_buffer_to_offset_boundary(buffer_id, left_boundary,
                                right_boundary);

                                /* if whole buffer not filled, then there were not 
                                enough bytes in the sstable to fill the buffer, hence 
                                there are no bytes in the sstable beyond the value of
                                right_boundary 
                                */
                                if (cqes[j]->res <
                                fair_aligned_sstable_page_cache_buffer_size) {
                                    sstable_info->cache_helper.
                                    set_cur_min_invalid_offset(right_boundary + 1);
                                }
                            } else {
                                /* if no bytes read, tell io_uring to free up the buffer
                                (it is the most recently used by io_uring
                                hence will be the one removed by the
                                following advance call) and mark the
                                beginning offset as invalid
                                */
                                sstable_info->cache_helper.
                                set_cur_min_invalid_offset(left_boundary);
                                io_uring_buf_ring_advance(
                                    sstable_info->buffer_ring, 1);
                            }
                        }
                        io_uring_cq_advance(sstable_info->io_ring,
                        num_chunks_queued);
                        this->try_sstable_tree_read_in_memory(sstable_info, 
                        scheduler_ring, conn_req, location, buffer_in_cache, 
                        sstable_num, level, left_boundary, null_key, tombstone_value, 
                        level_infos, index_in_level);
                    }
            
                }
                sstable_num = i;
                j = 0;
                level = sstable_num / LEVEL_FACTOR;
                index_in_level = sstable_num - (level * LEVEL_FACTOR);
                sstable_info = &(level_infos[level].sstable_infos[index_in_level]);
                prev_sstable_num = sstable_num - LEVEL_FACTOR;
                prev_level = level - 1;
                prev_index_in_level = prev_sstable_num - (prev_level 
                * LEVEL_FACTOR);
                prev_buffer_queue = level_infos[prev_level].buffer_queues 
                + prev_sstable_num;

                if (!sstable_info->waiting_on_io) {
                    /* First check sstable wait queue for incoming read batches or 
                    compactions. Should not have to contend for long since enqueuing is 
                    simple and fast
                    */
                    while (!sstable_info->req_batch_wq.guard.is_single_thread && 
                    !sstable_info->req_batch_wq.guard.atomic_consumer_guard.compare_exchange_weak(
                        my_zero, 1)) [[unlikely]];
                    sstable_info->req_batch = sstable_info->req_batch_wq
                    .pop_front();
                    sstable_info->req_batch_wq.guard.atomic_consumer_guard.store(0);
                    my_zero = 0;
                    if (sstable_info->req_batch) {
                        if (sstable_info->req_batch->req_type == READ) {
                            if (!sstable_info->req_batch->content.readwrite_pool
                            .present_in_level) {
                                this->insert_into_wqs(level + 1, level_infos, 
                                sstable_info->req_batch, sstable_info, index_in_level);
                            } else if (!sstable_info->is_flushed_to) {
                                if (level < NUM_SSTABLE_LEVELS - 1) {
                                    this->insert_into_wqs(level + 1, level_infos, 
                                    sstable_info->req_batch, sstable_info, 
                                    index_in_level);
                                } else {
                                    /* this is the last level, so keys with unfilled 
                                    values are not in the database
                                    */
                                    sqe = io_uring_get_sqe(sstable_info->io_ring);
                                    io_uring_prep_msg_ring(sqe, network_ringfd, 
                                    0, *(std::launder(reinterpret_cast<__u64*>(
                                        &sstable_info->req_batch))), 0);
                                    io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
                                }
                            } else {
                                this->try_add_buffers_nonblockingly(*sstable_info, 
                                *buffer_queue);
                                /* start reading from the sstable for the request batch 
                                by using the page cache, scheduling asynchronous file 
                                I/O requests when needed
                                */
                                sstable_info->desired_sstable_offset = 0;
                                sstable_info->stack.push_top(0, sstable_info->req_batch
                                ->content.readwrite_pool.length >> 1, 0, sstable_info->req_batch
                                ->content.readwrite_pool.length);
                                this->try_sstable_tree_read_in_memory(sstable_info, 
                                scheduler_ring, conn_req, location, buffer_in_cache, 
                                sstable_num, level, left_boundary, null_key, 
                                tombstone_value, level_infos, index_in_level);
                            }

                        } else if (sstable_info->is_flushed_to && 
                        level < NUM_SSTABLE_LEVELS - 1) {
                            this->insert_into_wqs(level + 1, level_infos, 
                            sstable_info->req_batch, sstable_info, index_in_level);
                        } else if (!sstable_info->is_flushed_to) {
                            sqe = io_uring_get_sqe(sstable_info->io_ring);
                            io_uring_prep_write(sqe, sstable_num, 
                            sstable_info->req_batch->content.req_seg.data, 
                            (1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20) * sizeof(CacheBufferEntry), 0);
                            io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE | IOSQE_IO_LINK
                            | IOSQE_CQE_SKIP_SUCCESS);

                            sqe = io_uring_get_sqe(sstable_info->io_ring);
                            io_uring_prep_msg_ring(sqe, scheduler_ring->ring_fd, 
                            0, static_cast<__u64>(sstable_num), 0);
                            io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);

                            io_uring_submit(sstable_info->io_ring);
                            sstable_info->waiting_on_io = true;
                        } else {
                            /* otherwise, the current sstable is on the last level and is 
                            already flushed to
                            */
                            delete sstable_info->req_batch;
                        }                    
                    } else if (!sstable_info->already_waited) {
                        /* give a chance for another sstable (which may be in the 
                        same thread as the current sstable) to put a request batch 
                        in the latter's request batch wait queue, but do not wait 
                        too long, wait only one loop cycle
                        */
                        sstable_info->already_waited = true;
                    } else {
                        /* try to get access to the buffer queue in a lock-free manner
                        */
                        if (level > 0 && !buffer_queue->guard.is_single_thread && 
                        !buffer_queue->guard.atomic_guard.compare_exchange_weak(
                            my_zero, 1)) [[unlikely]] { 
                        /* another thread is giving buffers; just wait until the next 
                        while loop iteration for the buffer queue to become available
                        */
                       } else if (level > 0 && buffer_queue->num_new_buffers) { 
                            // collect at most the first fair number of new buffers
                            int at_most_fair = std::min(max_sstable_height,
                            buffer_queue->num_new_buffers);
                            for (num_new_buffers = 0; num_new_buffers < at_most_fair; 
                            ++num_new_buffers) {
                            new_buffers[num_new_buffers] = buffer_queue->pop_front();
                            }
                            if (!buffer_queue->guard.is_single_thread) [[unlikely]] {
                                buffer_queue->guard.atomic_guard.store(0);
                                my_zero = 0;
                            }

                            // transfer these buffers upstream to the previous sstable

                            while (!prev_buffer_queue->guard.is_single_thread && 
                            !prev_buffer_queue->guard.atomic_guard.compare_exchange_weak(
                                my_zero, 1))  [[unlikely]];
                            while (num_new_buffers--) {
                                prev_buffer_queue->push_back(new_buffers[
                                    num_new_buffers]);
                            }   
                            if (!prev_buffer_queue->guard.is_single_thread) [[unlikely]] {
                                prev_buffer_queue->guard.atomic_guard.store(0);
                                my_zero = 0;
                            }
                        } else if (level > 0) { 
                            // wait before transferring buffers upstream again
                            sstable_info->already_waited = false;

                            /* No new buffers, must transfer buffers upstream from 
                            existing stock if any. No new buffers means 
                            buffer_queue.num_new_buffers = 0 and hence 
                            buffer_queue.cur_num_buffers is precisely the number of old
                            (already registered in the current sstable) buffers. By 
                            this formula, no sstable will ever run out of buffers.
                            */
                            unsigned int to_give = buffer_queue->cur_num_buffers ? 
                            std::max(std::min(buffer_queue->cur_num_buffers, 
                            max_sstable_height) / LEVEL_FACTOR, 1U) : 0;
                            if (to_give) {
                                buffer_queue->cur_num_buffers -= to_give;
                                if (!buffer_queue->guard.is_single_thread) [[unlikely]] {
                                    buffer_queue->guard.atomic_guard.store(0);
                                    my_zero = 0;
                                }
                                while (!prev_buffer_queue->guard.is_single_thread && 
                                !prev_buffer_queue->guard.atomic_guard.compare_exchange_weak(
                                    my_zero, 1))  [[unlikely]];
                                
                                auto original_insert_from = sstable_info->insert_buffers_from;
                                for (int i = 0; i < to_give; ++i) {
                                    prev_buffer_queue->push_back(
                                        sstable_info->page_cache_buffers[
                                            --sstable_info->insert_buffers_from]);
                                }
                                /* remove entries from sparse buffer index by
                                buffer id
                                */

                                sstable_info->cache_helper.remove_buffer_range(
                                    ++sstable_info->insert_buffers_from, original_insert_from
                                );
                                if (!prev_buffer_queue->guard.is_single_thread) [[unlikely]] {
                                    prev_buffer_queue->guard.atomic_guard.store(0);
                                    my_zero = 0;    
                                }   
                                this->reregister_buffer_ring(sstable_info);   
                            }
                        }
                    }
                }
            }
        }

    }

    private:
    // start the network thread and start processing incoming requests
    void start_ringdb(auto network_callback) {

        std::thread t(network_callback, this, &this->network_ring_fd);
        t.detach();

        // start processing incoming requests...
        char* tombstone_value = new char[VALUE_LENGTH];
        std::fill(tombstone_value, tombstone_value + VALUE_LENGTH, '\0');
        char* null_key = new char[KEY_LENGTH];
        std::fill(null_key, null_key + KEY_LENGTH, '\0');
        struct io_uring* main_ring = this->main_thread_comm_ring;
        SparseIndex memtable_sparse_index;
        RequestBatch* read_batch = new RequestBatch(READ);
        RequestBatch* compaction_batch = new RequestBatch(COMPACTION);
        struct io_uring_cqe* cqe;
        struct io_uring_sqe* sqe;
        unsigned int table_num;
        while (true) {
            sqe = io_uring_get_sqe(main_ring);
            io_uring_wait_cqe(main_ring, &cqe);
            io_uring_cqe_seen(main_ring, cqe);
            RequestBatch* req_batch = *std::launder(reinterpret_cast<RequestBatch**>
            (std::launder(reinterpret_cast<uintptr_t*>(&cqe->user_data))));
            ReadWritePool& rw_pool = req_batch->content.readwrite_pool;

            char *key, *value;
            for (unsigned int i = 0; i < rw_pool.length; ++i) {
                ConnectionRequest& conn_req = rw_pool.data[i];
                key = conn_req.buffer + 5;
                value = key + KEY_LENGTH;
                table_num = 
                memtable_sparse_index.get_table_num(key);
                unsigned char my_zero = 0;
                // use the sparse index first
                if (!std::memcmp(key, null_key, KEY_LENGTH)) {
                    std::fill(conn_req.buffer, conn_req.buffer + SOCKET_BUFFER_LENGTH, 
                    '\0');
                } else if (conn_req.req_type == READ) {
                    if (table_num == -1 || !this->memtables[table_num].read(key, value)) {
                        read_batch->insert_read_write(
                            conn_req.buffer, 
                            conn_req.client_sock_fd,
                            READ
                        );
                        if (read_batch->content.readwrite_pool.length == BATCH_NUM_REQUESTS) {
                            // send read batch to level 1
                            this->insert_into_wqs(0, this->level_infos, read_batch);
                        }
                        rw_pool.remove_via_index(i);
                    }
                } else if (this->memtables[table_num].size < (1 << MEMTABLE_SIZE_MB_BITS) * 
                (1 << 20)) {
                    this->memtables[table_num].write(key, value, table_num, 
                    memtable_sparse_index);
                } else { // memtable is full, must compact to level 1 and then clear memtable
                    if (this->is_practically_full) {
                        /* signify that write may ultimately fail because database is 
                        practically full
                        */
                        memcpy(conn_req.buffer, conn_req.buffer + 5, '\0');
                    }
                    memtable_sparse_index.remove_table(table_num);
                    compaction_batch->content.req_seg.insert_memtable(
                        this->memtables[table_num].data);
                    this->memtables[table_num].empty_out();

                    this->insert_into_wqs(0, this->level_infos, compaction_batch);

                    this->memtables[table_num].write(key, value, table_num, 
                    memtable_sparse_index);
                }
            }
            // submit writes and the reads that were in the memtables
            io_uring_prep_msg_ring(sqe, this->network_ring_fd, 0, 
            *std::launder(reinterpret_cast<__u64*>(&req_batch)), 0);
            io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
            io_uring_submit(main_ring);
        }

    }


    inline void try_add_buffers_nonblockingly(SSTableInfo& sstable_info,
    BufferQueue& buffer_queue) {
        unsigned char my_zero = 0;
        /* Due to integer division rounding, the sstable will
        always have at least LEVEL_FACTOR - 1 buffers, so we can check the
        buffer ring nonblockingly for new buffers instead of blockingly.
        Use weak CAS (LL/SC) instead of strong even in non-blocking case 
        because there will generally be a large number of request batches 
        being processed concurrently by the LSM tree and so the guaranteed
        latency overhead of strong CAS on nearly every context switch is not
        worth the possible extra gain in I/O speed by having extra buffers.
        */
        if (buffer_queue.guard.is_single_thread || 
        buffer_queue.guard.atomic_guard.compare_exchange_weak(my_zero, 1)) 
            [[likely]] {
                int new_num_added = 0;
                char* new_buffer;
                while (buffer_queue.front()) {
                    ++new_num_added;
                    new_buffer = buffer_queue.pop_front();
                    sstable_info.page_cache_buffers[
                    sstable_info.insert_buffers_from] = new_buffer;
                    io_uring_buf_ring_add(sstable_info.buffer_ring, new_buffer,
                    fair_aligned_sstable_page_cache_buffer_size, 
                    sstable_info.insert_buffers_from, io_uring_buf_ring_mask(
                    max_buffer_ring_size), 
                    sstable_info.insert_buffers_from++);
                    sstable_info.cache_helper.add_buffer();
                }
                if (!buffer_queue.guard.is_single_thread) [[unlikely]] {
                    buffer_queue.guard.atomic_guard.store(0);
                }
                io_uring_buf_ring_advance(sstable_info.buffer_ring, 
                new_num_added);
        }
    }

    inline void reregister_buffer_ring(SSTableInfo* sstable_info) {
        /* only formally unregister buffer ring, do not free its underlying memory, which 
        io_uring_free_buf_ring does
        */
        io_uring_unregister_buf_ring(sstable_info->io_ring, 
        sstable_info->buffer_ring_id);
        struct io_uring_buf_reg reg = { };
        reg.ring_addr = (size_t)sstable_info->buffer_ring;
        reg.ring_entries = max_buffer_ring_size;
        reg.bgid = sstable_info->buffer_ring_id;
        io_uring_register_buf_ring(sstable_info->io_ring, &reg, 0);
        io_uring_buf_ring_init(sstable_info->buffer_ring);
        int buffer_id;
        // to prevent errors, do not add buffers that survived the transferring out
        for (int i = sstable_info->cache_helper.get_id_of_most_recently_selected_buffer() + 1; 
        i < sstable_info->insert_buffers_from; ++i) {
            io_uring_buf_ring_add(sstable_info->buffer_ring, 
            sstable_info->page_cache_buffers[i],
            fair_aligned_sstable_page_cache_buffer_size, i,
            io_uring_buf_ring_mask(reg.ring_entries), i
            );
        }
        io_uring_buf_ring_advance(sstable_info->buffer_ring, 
        sstable_info->insert_buffers_from);
    }

    /* Insert the RequestBatches of the
    decomposition into the sstables' respective
    wait queues at the specified level (plus 1 since the levels
    are zero-indexed but here they refer to only sstable levels which start at 1
    in the LSM tree design) as well as, if the current level is at least 1 (in the
    LSM tree design), the correct number of buffers in the 
    sstables' respective buffer queues. Do all insertions in a lock-free manner.
    */
    inline void insert_into_wqs(unsigned int level,
    LevelInfo* level_infos, RequestBatch* batch,
    SSTableInfo* sstable_info = nullptr, unsigned int index_in_level = -1) {
        unsigned char my_zero = 0;
        while (!level_infos[level].guard.is_single_thread && 
        !level_infos[level].guard.atomic_guard.compare_exchange_weak(
            my_zero, 1)) [[unlikely]];

        RequestType req_type = batch->req_type;
        Decomposition decomp = level_infos[level].decompose(batch);
        if (!level_infos[level].guard.is_single_thread) [[unlikely]] {
            level_infos[level].guard.atomic_guard.store(0);
        }

        delete batch;
        if (!level) {
            batch = new RequestBatch(req_type);
        }

        bool inserted[LEVEL_FACTOR];
        for (unsigned int r = 0; r < LEVEL_FACTOR; ++r) {
            inserted[r] = false;
        }
        bool could_contend_head[LEVEL_FACTOR];
        for (unsigned int r = 0; r < LEVEL_FACTOR; ++r) {
            could_contend_head[LEVEL_FACTOR] = false;
        }

        unsigned int num_queued = 0;
        RequestBatch** batches = decomp.decomposition;
        RequestBatchWaitQueue* wq;
        double proportions[LEVEL_FACTOR];
        for (int i = 0; i < LEVEL_FACTOR; ++i) {
            proportions[i] = decomp.expected_sstable_sizes[i] 
            / decomp.total_decomp_size;
        }
        /* Must give buffers to sstables that have less than the fair
        number of buffers based on expected new sstable size using 
        precomputed proportion values. Send a total of 1/LEVEL_FACTOR of 
        the original sstable's buffers because the other LEVEL_FACTOR
        - 1 sstables on the same level will do the same and hence the
        total number contributed by the original sstable's level will be
        significant yet also fair since each sstable will give a constant
        and uniform proportion of its buffers; also, the original sstable
        can get more buffers if it needs more from either the sstables
        above it or from these lower sstables themselves once the latter
        no longer need them due to inactivity.
        */
        char* new_buffers[max_sstable_height];
        int num_new_buffers = 0;
        int old_buffer_limit, old_num_old_buffers;
        if (level) {
            BufferQueue& buffer_queue = level_infos[level - 1].buffer_queues[index_in_level];
            while (!buffer_queue.guard.is_single_thread && 
            !buffer_queue.guard.atomic_guard.compare_exchange_weak(
                my_zero, 1)) [[unlikely]];
            old_num_old_buffers = buffer_queue.cur_num_buffers - 
            buffer_queue.num_new_buffers;
            old_buffer_limit = old_num_old_buffers ? std::max(
                static_cast<int>(old_num_old_buffers / LEVEL_FACTOR),
                1
            ) : 0;
            int new_buffer_limit = buffer_queue.num_new_buffers ? : std::max(std::min(
                max_sstable_height,
                buffer_queue.num_new_buffers / LEVEL_FACTOR
            ), static_cast<unsigned int>(1));
            for (int num_new_buffers = 0; num_new_buffers < new_buffer_limit
            ; ++num_new_buffers) {
                new_buffers[num_new_buffers] = buffer_queue.pop_front();
            }

            /* add remaining new buffers to current sstable's buffer ring
            while it still has access
            */
            int num_added_to_current = buffer_queue.num_new_buffers;
            while (buffer_queue.num_new_buffers) {
                char* new_buffer = buffer_queue.pop_front();
                sstable_info->page_cache_buffers[sstable_info->insert_buffers_from] 
                = new_buffer;
                sstable_info->cache_helper.add_buffer();
                io_uring_buf_ring_add(sstable_info->buffer_ring,
                new_buffer, fair_aligned_sstable_page_cache_buffer_size,
                sstable_info->insert_buffers_from, io_uring_buf_ring_mask(
                    max_sstable_height* LEVEL_FACTOR), 
                    sstable_info->insert_buffers_from++);
            }
            io_uring_buf_ring_advance(sstable_info->buffer_ring, num_added_to_current);

            if (!buffer_queue.guard.is_single_thread) [[unlikely]] {
                buffer_queue.guard.atomic_guard.store(0);
                my_zero = 0;
            }
        }
        while (num_queued < decomp.num_req_batches) {
            for (unsigned int i = 0; i < LEVEL_FACTOR; ++i) {
                wq = &(level_infos[level].sstable_infos[i].req_batch_wq);
                if (batches[i] && !inserted[i] && (wq->guard.is_single_thread || 
                wq->guard.atomic_producer_guard.compare_exchange_weak(my_zero, 1))) {
                    /* Must use strong ordering for both CAS and following store so
                    that the following wq.push_back operation is always committed 
                    to memory in the order of operations listed here, not out of
                    order.
                    */
                    could_contend_head[i] = !wq->try_push_back(batches[i], could_contend_head[i]);

                    if (!could_contend_head[i]) { // successful
                        if (batches[LEVEL_FACTOR]) {
                            wq->not_found_push_back(batches[LEVEL_FACTOR]);
                            batches[LEVEL_FACTOR] = nullptr;
                        }
                        ++num_queued;
                        inserted[i] = true;
                    }

                    if (!wq->guard.is_single_thread) [[unlikely]] {
                        wq->guard.atomic_producer_guard.store(0);
                        my_zero = 0;
                    }
                }
            }
        }

        /* Must give buffers to sstables that have less than the fair
        number of buffers based on expected new sstable size using 
        precomputed proportion values.
        */
        if (level && (old_buffer_limit || num_new_buffers)) {
                bool given_buffers[LEVEL_FACTOR];
                for (unsigned int r = 0; r < LEVEL_FACTOR; ++r) {
                    given_buffers[r] = false;
                }
                int num_given = 0;
                int num_old_buffers_given = 0;
                int num_new_buffers_given = 0;
                int proportional_old_quantity, proportional_new_quantity, j;            
                BufferQueue* bq;
                while (num_given < decomp.num_req_batches) {
                    for (int i = 0; i < LEVEL_FACTOR; ++i) {
                        bq = &(level_infos[level].buffer_queues[i]);
                        if (batches[i] && !given_buffers[i] && 
                        (bq->guard.is_single_thread || 
                        bq->guard.atomic_guard.compare_exchange_weak(my_zero, 1))) {
                            if (bq->cur_num_buffers < max_sstable_height) {
                                given_buffers[i] = true;
                                ++num_given;

                                proportional_old_quantity = proportions[i] * 
                                old_buffer_limit;
                                proportional_new_quantity = proportions[i] *
                                num_new_buffers;

                                auto original_insert_from = sstable_info->insert_buffers_from;
                                for (j = 0; j < proportional_old_quantity && 
                                num_old_buffers_given < old_buffer_limit; ++i) {
                                    bq->push_back(sstable_info->page_cache_buffers[
                                        --sstable_info->insert_buffers_from]);
                                    ++num_old_buffers_given;
                                };
                                sstable_info->cache_helper.remove_buffer_range(
                                    ++sstable_info->insert_buffers_from, original_insert_from
                                );
                                for (j = 0; j < proportional_new_quantity && 
                                num_new_buffers_given < num_new_buffers; ++j) {
                                    bq->push_back(new_buffers[num_new_buffers_given++]);
                                }

                                if (num_given == decomp.num_req_batches) {
                                    for (; num_new_buffers_given < num_new_buffers;
                                    ++num_new_buffers_given) {
                                        bq->push_back(new_buffers[num_new_buffers_given]);
                                    }
                                }

                                if (!bq->guard.is_single_thread) [[unlikely]] {
                                    bq->guard.atomic_guard.store(0);
                                    my_zero = 0;
                                }

                            } else if (!bq->guard.is_single_thread) [[unlikely]] {
                                bq->guard.atomic_guard.store(0);
                                my_zero = 0;
                            }
                        }
                    }
                }
                /* remove transferred buffers from current sstable's buffer ring
                */
                if (num_old_buffers_given) {
                    this->reregister_buffer_ring(sstable_info);
                }
            }
    }

    public:
    void network_thread_routine(int* network_ring_fd) {
        /* copy the main thread ring fd into a new local variable
        to access it without causing false sharing (do the same for sstables)
        */
        int main_thread_ring_fd = this->main_thread_comm_ring->ring_fd;

        pthread_t network_thread = pthread_self();
        cpu_set_t network_thread_cpu;
        CPU_ZERO(&network_thread_cpu);
        CPU_SET(1, &network_thread_cpu);
        pthread_setaffinity_np(
            network_thread, 
            sizeof(cpu_set_t), 
            &network_thread_cpu
        );

        struct io_uring* network_ring;
        io_uring_params network_thread_params;
        memset(&network_thread_params, 0, sizeof(io_uring_params));
        int main_ring_fd = this->main_thread_comm_ring->ring_fd;
        network_thread_params.wq_fd = main_ring_fd;
        network_thread_params.flags =  IORING_SETUP_ATTACH_WQ
        | IORING_SETUP_SUBMIT_ALL
        | IORING_SETUP_COOP_TASKRUN
        | IORING_SETUP_TASKRUN_FLAG
        | IORING_SETUP_SINGLE_ISSUER
        | IORING_SETUP_DEFER_TASKRUN;
        network_thread_params.features = IORING_FEAT_FAST_POLL;
        // add 1 for the single lasting accept multishot request
        io_uring_queue_init_params(MAX_SIMULTANEOUS_CONNECTIONS + 1,
        network_ring, &network_thread_params);
        io_uring_register_ring_fd(network_ring);
        *network_ring_fd = network_ring->ring_fd;

        struct sockaddr_in6 server_addr, client_addr;
        server_addr.sin6_family = AF_INET6;
        server_addr.sin6_addr = in6addr_any;
        server_addr.sin6_port = htons(PORT);
        socklen_t client_len = sizeof(client_addr);
        int server_sock_fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
        const int reuse_val = 1;
        setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_val, sizeof(int));

        bind(server_sock_fd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in6));
        // balance between processing new connections and finishing current ones
        listen(server_sock_fd, MAX_SIMULTANEOUS_CONNECTIONS >> 1);

        io_uring_register_files_sparse(network_ring, 1 + MAX_SIMULTANEOUS_CONNECTIONS);
        this->fixed_file_fds[NUM_FILE_FDS - 1] = server_sock_fd;
        io_uring_register_files_update(network_ring, 0,
        this->fixed_file_fds + NUM_SSTABLE_WORKER_THREADS + NUM_FILE_FDS - 1, 1);

        /* Align each socket buffer in the socket buffer pool to avoid false sharing
        */
        char* socket_buffers = (char*)mmap(
            nullptr,
            std::lcm(SOCKET_BUFFER_LENGTH, ALIGN_NO_FALSE_SHARING) * 
            MAX_SIMULTANEOUS_CONNECTIONS, 
            PROT_READ | PROT_WRITE,
            /* MAP_HUGETLB uses the default huge page size on the host platform, so 
            make sure to change that if applicable
            */
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED 
            | (PAGE_SIZE > 4096) ? MAP_HUGETLB : 0,
            -1, 
            0
        );

        // now start receiving and processing incoming connections
        struct io_uring_sqe* sqe1 = io_uring_get_sqe(network_ring);
        RequestBatch accept_batch(ACCEPT);
        RequestBatch* accept_batch_ptr = &accept_batch;
        io_uring_prep_multishot_accept_direct(sqe1, 0, nullptr, nullptr, 0);
        io_uring_sqe_set_flags(sqe1, IOSQE_FIXED_FILE);
        io_uring_sqe_set_data64(sqe1, *std::launder(reinterpret_cast<__u64*>(&accept_batch_ptr)));

        RequestBatch* batch = new RequestBatch(READ);

        ConnectionPool* connection_pool = new ConnectionPool(
            MAX_SIMULTANEOUS_CONNECTIONS, socket_buffers);

        // send the network ring fd to each worker thread's scheduler ring
        struct io_uring_sqe* sqe;
        for (unsigned int i = 0; i < NUM_SSTABLE_WORKER_THREADS; ++i) {
            sqe = io_uring_get_sqe(network_ring);
            io_uring_prep_msg_ring(sqe, this->fixed_file_fds[i], 0, 
            static_cast<__u64>(network_ring->ring_fd), 0);
            io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
            io_uring_submit(network_ring);
        }

        char* null_key = new char[KEY_LENGTH];
        std::fill(null_key, null_key + KEY_LENGTH, '\0');
        #ifdef RINGDB_TEST
        std::thread t(test_ringdb, nullptr);
        t.detach();
        #endif
        while (true) {
            /* efficiently simultaneously submit requests and wait for completions
            */
            io_uring_submit_and_wait(network_ring, BATCH_NUM_REQUESTS);
            struct io_uring_cqe* cqe;
            sqe = io_uring_get_sqe(network_ring);

            std::uint_fast32_t head, i = 0;
            ConnectionRequest* conn_req;
            BaseRequest* cqe_base_req;
            RequestType req_type;
            // client sock fd will actually be an io_uring direct descriptor
            int client_sock_fd, socket_buffer_id;
            char* socket_buffer;
            io_uring_for_each_cqe(network_ring, head, cqe) {
                client_sock_fd = cqe->res;
                cqe_base_req = *std::launder(reinterpret_cast<BaseRequest**>
                (std::launder(reinterpret_cast<uintptr_t*>(&cqe->user_data))));
                req_type = cqe_base_req->req_type;

                if (req_type == ACCEPT) {
                    if (client_sock_fd > 0) [[likely]] {
                        // prepare recv request
                        io_uring_prep_recv(sqe, client_sock_fd, conn_req->buffer, 
                        SOCKET_BUFFER_LENGTH, 0);
                        io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
                        io_uring_sqe_set_data64(sqe, 
                        *std::launder(reinterpret_cast<__u64*>(connection_pool->insert(
                            client_sock_fd, RECV))));
                        sqe->buf_group = 0;
                        if (!(cqe->flags & IORING_CQE_F_MORE)) [[unlikely]] {
                            /* old multishot accept request is dead due to no
                            new connection requests coming in recently, so need to insert 
                            a new multishot accept request
                            */ 
                            struct io_uring_sqe* sqe2 = io_uring_get_sqe(
                            network_ring);
                            io_uring_prep_multishot_accept(sqe2, 0, 
                            nullptr, nullptr, 0);
                            io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
                            io_uring_sqe_set_data64(sqe2, *std::launder(
                                reinterpret_cast<__u64*>(&accept_batch_ptr)));
                        } 
                    }
                    /* Otherwise, no more connections available, so just close it. In practice, 
                    the database should be distributed and a load balancer
                    should reroute overflowing connections to other database
                    nodes.
                    */
                } else if (req_type == RECV) {
                    conn_req = static_cast<ConnectionRequest*>(cqe_base_req);
                    socket_buffer = conn_req->buffer;
                    std::fill(socket_buffer + 5 + KEY_LENGTH, socket_buffer + 
                    VALUE_LENGTH, '\0');

                    /* Enum value should be READ (3) if the result of 
                    std::memcmp is zero and WRITE (4) otherwise. No branch 
                    prediction needs to be used here after the std::memcmp 
                    operation because, if std::memcmp returns zero, then
                    req_type = !!0 = !(!0) = !1 =  0, and if std::memcmp 
                    returns a nonzero value x, then !!x = !(!x) = !0
                    = 1.
                    */
                    req_type = static_cast<RequestType>(
                        (!!std::memcmp(socket_buffer, "read", 5)) + 3);

                    client_sock_fd = conn_req->client_sock_fd;
                    if (batch->content.readwrite_pool.length < BATCH_NUM_REQUESTS) {
                        batch->insert_read_write(socket_buffer, 
                        client_sock_fd, req_type);
                    } else {
                        /* convert __u64 to uintptr_t and then to the 
                        desired pointer type RequestBatch*
                        */
                        io_uring_prep_msg_ring(sqe, main_ring_fd, 0,
                        *std::launder(reinterpret_cast<__u64*>(&batch)), 0);
                        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
                        batch = new RequestBatch(READ);
                    }
                /* The following two branches are structured this way, in
                particular with no [[unlikely]] attribute, to not stall
                the pipeline during a branch prediction.
                */
                } else if (req_type == SEND 
                    && !(cqe->flags & IORING_CQE_F_MORE)) {
                        // zero-copy send done, close connection
                        conn_req = static_cast<ConnectionRequest*>(cqe_base_req);
                        RequestBatch* req_batch_ref = static_cast<RequestBatch*>(
                            conn_req->req_batch_ref);
                        client_sock_fd = conn_req->client_sock_fd;
                        io_uring_prep_close_direct(sqe, client_sock_fd);
                        if (++req_batch_ref->content.readwrite_pool.num_sent == 
                        req_batch_ref->content.readwrite_pool.length) {
                            delete req_batch_ref;
                        }
                        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
                    // else, zero-copy send not completed, need to wait again
                } else if (req_type == READ || req_type == WRITE) { 
                    // completed read or write batch
                    RequestBatch* req_batch = 
                    static_cast<RequestBatch*>(cqe_base_req);
                    ReadWritePool& rw_pool = req_batch->content.readwrite_pool;
                    ConnectionRequest* entries = rw_pool.data;
                    unsigned int length = rw_pool.length;

                    /* By reusing the same sqe variable in this loop, we 
                    can reuse the old SQ entry value above, so we can 
                    ensure that all the SQ entries in the SQ ring are 
                    contiguous. Also, we never have to risk a new register a
                    allocation in the loop when we get a new available SQ 
                    location. Finally, by using the variable entries with 
                    the loop variable i in this way, we don't have to add 
                    a new value of i to entries twice every iteration, so
                    the CPU won't have a redundant operation to execute in
                    its pipeline; on the other hand, we also won't have to
                    store the value of entries + i in a local variable in 
                    each iteration of the loop, so again we won't risk
                    a new register allocation and can reuse the old register
                    containing the value of the entries pointer. It is good
                    to use the fast int variable i for the loop condition 
                    instead of a pointer comparison with entries because we 
                    need only integer-level comparisons for the loop 
                    condition and int_fast32_t is the fastest type for 
                    integer arithmetic, including comparisons.
                    */
                    for (; i < length - 1; ++i,
                    ++entries) {
                        if (entries->client_sock_fd != -1) {
                            io_uring_prep_send_zc(sqe, entries->client_sock_fd,
                            entries->buffer, SOCKET_BUFFER_LENGTH, 0, 0);
                            io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
                            io_uring_sqe_set_data64(sqe, *std::launder(
                            reinterpret_cast<__u64*>(&entries))
                            );
                            sqe = io_uring_get_sqe(network_ring);
                        } else {
                            // increment this since length never decreases
                            ++req_batch->content.readwrite_pool.num_sent;
                        }
                    }
                    // handle last entry separately to not mistakenly get sqe twice
                    if (entries->client_sock_fd != -1) {
                        io_uring_prep_send_zc(sqe, entries->client_sock_fd,
                        entries->buffer, SOCKET_BUFFER_LENGTH, 0, 0);
                        io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
                        io_uring_sqe_set_data64(sqe, *std::launder(
                            reinterpret_cast<__u64*>(&entries))
                            );
                    } else {
                        // increment this since length never decreases
                        ++req_batch->content.readwrite_pool.num_sent;
                    }

                    if (req_batch->content.readwrite_pool.num_sent == req_batch->content
                    .readwrite_pool.length) {
                        delete req_batch;
                    }
                    i = 0;
                }
            } 
            /* try to do extra batching with completion queue finishing, using only 
            one store memory barrier
            */
            io_uring_cq_advance(network_ring, BATCH_NUM_REQUESTS);
        }
    }

    private:
    /* This and all its substructures should be instantiated in the cpp
    file. The sstable and network rings should be initiated with the 
    flags IORING_SETUP_SQPOLL, IORING_SETUP_SUBMIT_ALL, IORING_SETUP_ATTACH_WQ,
    IORING_SETUP_R_DISABLED (not the network ring), 
    IORING_SETUP_COOP_TASKRUN, 
    IORING_SETUP_TASKRUN_FLAG (the latter two not for the sstable scheduler rings),
    IORING_SETUP_SINGLE_ISSUER,and IORING_SETUP_DEFER_TASKRUN. The main thread's ring 
    should have IORING_SETUP_SQ_AFF and not use IORING_SETUP_R_DISABLED. The
    network ring should use IORING_FEAT_FAST_POLL and 
    could use IORING_SETUP_CQSIZE. We can also use the feature flag 
    IORING_FEAT_NATIVE_WORKERS.
    */
    LevelInfo* level_infos;


    // file descriptor for the directory '/sstable'
    int sstable_dir_fd;

    /* Shared table of file descriptors to be used by the io rings for registering
    non-ring file descriptors and initially communicating between io rings. The last 
    MAX_SIMULTANEOUS_CONNECTIONS entries will be provided to the kernel to allow direct client 
    socket descriptors to be used to reduce latency in the kernel during socket I/O. 
    The io ring fds will not actually be fixed file fds to other threads.
    */
    int* fixed_file_fds = new int[NUM_SSTABLE_WORKER_THREADS + NUM_FILE_FDS + 
    MAX_SIMULTANEOUS_CONNECTIONS];

    /* Copy the network thread ring fd into a new local variable
    to access it without causing false sharing (do the same for 
    sstables). By the time this will be used, it will definitely have 
    been defined since the network thread routine below will send 
    requests only after it has defined that variable.
    */
    int network_ring_fd;

    /* the database is practically full if at least one sstable at the last level is nonempty 
    (is flushed to)
    */
    bool is_practically_full = false;

    MemTable* memtables;
    struct io_uring* main_thread_comm_ring;

    std::atomic_uint num_read{0};

    char* sstable_page_cache_buffers; 
};
