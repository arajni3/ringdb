#include <liburing.h>
#include <iostream>
#include "lsm_tree.hpp"
#include "constinit_constants.hpp"

#include "aux_data_structures/aux_data_structures_impls/base_request/base_request.hpp"

#include "aux_data_structures/aux_data_structures_impls/connection_pool/connection_request/connection_request.hpp"
#include "aux_data_structures/aux_data_structures_impls/connection_pool/readwrite_pool/readwrite_pool.hpp"
#include "aux_data_structures/aux_data_structures_impls/connection_pool/connection_pool.hpp"

#include "aux_data_structures/aux_data_structures_impls/level_info/buffer_queue/buffer_queue.hpp"
#include "aux_data_structures/aux_data_structures_impls/level_info/decomposition/decomposition.hpp"
#include "aux_data_structures/aux_data_structures_impls/level_info/filter/filter.hpp"
#include "aux_data_structures/aux_data_structures_impls/level_info/sparse_index/sparse_index.hpp"
#include "aux_data_structures/aux_data_structures_impls/level_info/level_info.hpp"

#include "aux_data_structures/aux_data_structures_impls/level_zero/memtable.hpp"

#include "aux_data_structures/aux_data_structures_impls/sstable/request_batch/request_batch_wait_queue.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/request_batch/request_segment.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/request_batch/request_batch.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/sstable_cache_helper/cache_buffer_entry/cache_buffer_entry.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/sstable_cache_helper/sparse_buffer_index/sparse_buffer_index.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/sstable_cache_helper/cache_location.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/sstable_cache_helper/sstable_cache_helper.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/stack/stack.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/sstable_info.hpp"
#include "aux_data_structures/aux_data_structures_impls/sstable/sstable_info_unaligned.hpp"

#include "aux_data_structures/aux_data_structures_concepts/sstable/stack/stack.hpp"

int main(int, char**){
    typedef ConnectionRequest<BaseRequest> ConnReq;
    typedef ReadWritePool<ConnReq> ReadWritePoolType;

    typedef Stack<max_sstable_height> StackType;
    typedef RequestSegment<StackType, max_sstable_height> ReqSeg;
    typedef RequestBatch<BaseRequest, ReadWritePoolType, ReqSeg> ReqBatch;
    typedef ConnectionPool<ConnReq> ConnPool;
    typedef RequestBatchWaitQueue<ReqBatch> ReqBatchWaitQueue;

    typedef SparseBufferIndex<CacheLocation, max_buffer_ring_size> SparseBufferIndexType;
    typedef SSTableCacheHelper<SparseBufferIndexType, CacheLocation> SSTableCacheHelperType;
    
    typedef SSTableInfoUnaligned<ReqBatch, ReqBatchWaitQueue, SSTableCacheHelperType, StackType, 
    8 + level_str_len + 1 + sstable_number_str_len, max_buffer_ring_size> SSTableInfoUnalignedType;
    typedef SSTableInfo<ReqBatch, ReqBatchWaitQueue, SSTableCacheHelperType, StackType, 
    8 + level_str_len + 1 + sstable_number_str_len, max_buffer_ring_size, 
    alignof(SSTableInfoUnalignedType)> SSTableInfoType;

    typedef Filter<MEMTABLE_SIZE> FilterType;
    typedef BufferQueue<max_buffer_ring_size, max_sstable_height> BufferQueueType;
    typedef Decomposition<ReqBatch> DecompositionType;
    typedef LevelInfo<FilterType, BufferQueueType, SparseIndex, SSTableInfoType, 
    DecompositionType, ReqBatch, ReadWritePoolType, ConnReq> LevelInfoType;

    typedef MemTable<SparseIndex> MemTableType;

    typedef LSMTree<MemTableType, LevelInfoType, BufferQueueType, FilterType, SparseIndex, 
    DecompositionType, SSTableInfoType, SSTableCacheHelperType, CacheLocation, ReqBatch, 
    ReqBatchWaitQueue, ReqSeg, CacheBufferEntry, StackType, BaseRequest, ConnPool, 
    ConnReq, ReadWritePoolType> RingDB;

    /* Must call std thread using non-static functions, so use this ugly solution wherein we wrap 
    the instance methods behind static methods and internally pass "this" as the first callback 
    argument into std:thread; it works because obj.method(arg) is transformed by C++ into 
    method(&obj, arg).
    */
    RingDB* ringDB = new RingDB();
    ringDB->initialize(&RingDB::sstable_worker_thread, &RingDB::network_thread_routine);
}
