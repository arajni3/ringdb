#pragma once
#include <atomic>
#include "../../aux_data_structures_concepts/level_info/filter/filter.hpp"
#include "../../aux_data_structures_concepts/level_info/sparse_index/sparse_index.hpp"
#include "../../aux_data_structures_concepts/level_info/decomposition/decomposition.hpp"
#include "../../aux_data_structures_concepts/level_info/buffer_queue/buffer_queue.hpp"
#include "../../aux_data_structures_concepts/level_info/decomposition/decomposition.hpp"
#include "../../aux_data_structures_concepts/sstable/sstable_info.hpp"
#include "../../aux_data_structures_concepts/sstable/request_batch/request_batch.hpp"
#include "../../aux_data_structures_concepts/connection_pool/readwrite_pool/readwrite_pool.hpp"
#include "../../aux_data_structures_concepts/connection_pool/connection_request/connection_request.hpp"

/* largest members of this data structure are the array of filters and the array of sstable infos, so 
the formermost will be automatically aligned to avoid false sharing as long as the latter two are 
aligned to avoid false sharing
*/
template<FilterLike Filter, BufferQueueLike BufferQueue, SparseIndexLike SparseIndex, 
SSTableInfoLike SSTableInfo, DecompositionLike Decomposition, RequestBatchLike RequestBatch, 
ReadWritePoolLike ReadWritePool, ConnectionRequestLike ConnectionRequest>
struct LevelInfo {
    struct alignas(ALIGN_NO_FALSE_SHARING) {
        std::atomic_uchar atomic_guard{1};
        bool is_single_thread;
    } guard;

    Filter filters[LEVEL_FACTOR];
    SparseIndex sparse_index;
    BufferQueue buffer_queues[LEVEL_FACTOR];
    SSTableInfo sstable_infos[LEVEL_FACTOR];

    void set_is_single_thread(bool is_single_thread) {
        is_single_thread = is_single_thread;
        for (unsigned int i = 0; i < LEVEL_FACTOR; ++i) {
            buffer_queues[i].guard.is_single_thread = is_single_thread;
        }
    }

    /* do not delete req_batch in here because this will be called only when the current thread 
    is holding exclusive access to the current level info structure, and we do not want to hold 
    the access for longer than necessary by performing a heap write
    */
    Decomposition decompose(RequestBatch* req_batch) {
        Decomposition decomp;
        unsigned int i, j;
        for (i = 0; i < LEVEL_FACTOR; ++i) {
            decomp.decomposition[i] = nullptr;
            decomp.expected_sstable_sizes[i] = 0;
        }
        decomp.decomposition[LEVEL_FACTOR] = nullptr;
        i = 0;

        if (req_batch->req_type == READ) {
            bool initialized_batch[LEVEL_FACTOR + 1];
            for (i = 0; i <= LEVEL_FACTOR; ++i) {
                initialized_batch[i] = false;
            }
            int left_index, right_index;
            char* min_key, *max_key;
            ReadWritePool& rw_pool = req_batch->content.readwrite_pool;
            decomp.total_decomp_size = rw_pool.length;
            ConnectionRequest* conn_req;
            for (i = 0; i < LEVEL_FACTOR && !filters[i].empty(); ++i) {
                min_key = sparse_index.get_table_min_key(i);
                max_key = sparse_index.get_table_max_key(i);
                left_index = rw_pool.find_index_starting_from(0, min_key);
                right_index = rw_pool.find_index_starting_from(left_index, max_key);

                for (j = left_index; j < right_index; ++j) {
                    conn_req = rw_pool.data + j;
                    if (filters[i].prob_contains_key(conn_req->buffer + 5)) {
                        if (!initialized_batch[i]) [[unlikely]] {
                            decomp.decomposition[i] = new RequestBatch(READ);
                            decomp.decomposition[i]->content.readwrite_pool.present_in_level = 
                            true;
                            initialized_batch[i] = true;
                        }
                        decomp.decomposition[i]->insert_read_write(conn_req->buffer, 
                        conn_req->client_sock_fd, conn_req->req_type);
                        ++decomp.expected_sstable_sizes[i];
                    } else { // definitely not in this sstable hence in any sstable
                        if (!initialized_batch[LEVEL_FACTOR]) { [[unlikely]]
                            decomp.decomposition[LEVEL_FACTOR] = new RequestBatch(READ);
                            initialized_batch[i] = true;
                            decomp.decomposition[i]->content.readwrite_pool.present_in_level = 
                            false;
                        }
                        decomp.decomposition[LEVEL_FACTOR]->insert_read_write(
                            conn_req->buffer, conn_req->client_sock_fd, conn_req->req_type);
                        conn_req->client_sock_fd = -1;
                    }
                    conn_req->client_sock_fd = -1;

                }
            }
            for (i = 0; i < rw_pool.length; ++i) {
                conn_req = rw_pool.data + i;
                if (conn_req->client_sock_fd != -1) { // not contained in an sstable range
                    if (!initialized_batch[LEVEL_FACTOR]) { [[unlikely]]
                        decomp.decomposition[LEVEL_FACTOR] = new RequestBatch(READ);
                        decomp.decomposition[i]->content.readwrite_pool.present_in_level = false;
                    }
                    decomp.decomposition[LEVEL_FACTOR]->insert_read_write(
                        conn_req->buffer, conn_req->client_sock_fd, conn_req->req_type);
                }
            }
        } else { // compaction
            while (i < LEVEL_FACTOR && !filters[i++].empty());
            if (i < LEVEL_FACTOR) {
                decomp.decomposition[i] = req_batch;
                char* key = req_batch->content.req_seg.data;
                for (j= 0; j < (1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20); j += 
                KEY_LENGTH + VALUE_LENGTH) {
                    key += j;
                    /* ok to insert null key because not checking for null here improves 
                    performance, we we will check for null key everywhere else anyway, and 
                    adding one unnecessary key shouldn't significantly compromise filter 
                    accuracy
                    */
                    filters[i].insert_key(key);
                }
            } else {
                decomp.decomposition[0] = req_batch;
                i = 0;
            }
            decomp.total_decomp_size = (1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20);
            decomp.expected_sstable_sizes[i] = decomp.total_decomp_size;
        }

        return decomp;
    }
};

