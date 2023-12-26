#pragma once
#include "../../../aux_data_structures_concepts/base_request/base_request.hpp"
#include "../../../aux_data_structures_concepts/connection_pool/readwrite_pool/readwrite_pool.hpp"
#include "../../../aux_data_structures_concepts/sstable/request_batch/request_segment.hpp"
#include "../../../enums/request_type.hpp"

template<ReadWritePoolLike ReadWritePool, RequestSegmentLike RequestSegment>
union Content {
    ReadWritePool readwrite_pool;
    RequestSegment req_seg;
    Content() {}
};

template<BaseRequestLike BaseRequest, ReadWritePoolLike ReadWritePool, 
RequestSegmentLike RequestSegment>
struct RequestBatch : public BaseRequest {
    Content<ReadWritePool, RequestSegment> content;
    RequestBatch(RequestType req_type): BaseRequest(req_type) {}

    ~RequestBatch() {
        if (this->req_type == COMPACTION) {
            delete[] content.req_seg.data;
        } else {
            delete[] content.readwrite_pool.data;
        }
    }

    void insert_read_write(char* buffer, int client_sock_fd, RequestType req_type) {
        content.readwrite_pool.insert_read_write(buffer, client_sock_fd, req_type, 
        static_cast<void*>(this));
    }
};