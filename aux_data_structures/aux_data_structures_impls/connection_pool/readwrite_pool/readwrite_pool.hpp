#pragma once
#include "../../../enums/request_type.hpp"
#include "../../../aux_data_structures_concepts/connection_pool/connection_request/connection_request.hpp"

template<ConnectionRequestLike ConnectionRequest>
struct ReadWritePool {
    ConnectionRequest* data;
    unsigned int length = 0;
    unsigned int num_sent = 0;
    bool present_in_level;

    // guaranteed to succeed
    void insert_read_write(char* buffer, int client_sock_fd, 
    RequestType req_type, void* req_batch_ref) {
        if (!this->length) [[unlikely]] {
            this->data = new ConnectionRequest[PAGE_SIZE];
        }
        unsigned int left = 0, right = this->length, mid;
        ConnectionRequest* conn_req = this->data;
        while (left < right) {
            mid = (left + right) >> 1;
            conn_req = this->data + mid;
            if (std::memcmp(conn_req->buffer + 5, buffer + 5, KEY_LENGTH) < 0) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        // shift elements right if not inserting at the end
        if (left == this->length) {
            /* at the end of the last iteration of binary search, mid + 1 = left = old length, 
            or, if the length is zero (no binary search happened), then left = 0 and 
            conn_req = this->data = this->data + left
            */
            (conn_req = conn_req + (left > 0))->buffer = buffer;
            conn_req->client_sock_fd = client_sock_fd;
            conn_req->req_type = req_type;
            conn_req->req_batch_ref = req_batch_ref;
        } else {
            ConnectionRequest conn_req2, conn_req3;
            conn_req2.buffer = buffer;
            conn_req2.client_sock_fd = client_sock_fd;
            conn_req2.req_type = req_type;
            conn_req2.req_batch_ref = req_batch_ref;
            unsigned int i;
            conn_req = this->data + left;
            for (i = left; i < this->length; ++i, 
            ++conn_req) {
                conn_req3.buffer = conn_req->buffer;
                conn_req3.client_sock_fd = conn_req->client_sock_fd;
                conn_req3.req_type = conn_req->req_type;
                conn_req3.req_batch_ref = conn_req->req_batch_ref;

                conn_req->buffer = conn_req2.buffer;
                conn_req->client_sock_fd = conn_req2.client_sock_fd;
                conn_req->req_type = conn_req2.req_type;
                conn_req->req_batch_ref = conn_req2.req_batch_ref;

                conn_req2.buffer = conn_req3.buffer;
                conn_req2.client_sock_fd = conn_req3.client_sock_fd;
                conn_req2.req_type = conn_req3.req_type;
                conn_req2.req_batch_ref = conn_req2.req_batch_ref;
            }
            conn_req->buffer = conn_req2.buffer;
            conn_req->client_sock_fd = conn_req2.client_sock_fd;
            conn_req->req_type = conn_req2.req_type;
            conn_req->req_batch_ref = conn_req2.req_batch_ref;
        }
        ++this->length;
    }

    
    int find_index_starting_from(unsigned int start, char* buffer) {
        unsigned int left = start, right = this->length, mid;
        ConnectionRequest* conn_req;
        while (left < right) {
            mid = (left + right) >> 1;
            conn_req = this->data + mid;
            if (std::memcmp(conn_req->buffer + 5, buffer + 5, KEY_LENGTH) < 0) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        if (left == this->length || (!left && std::memcmp(this->data[0].buffer + 5, 
        buffer + 5, KEY_LENGTH))) {
            return -1;
        }
        return left;
    }

    void remove_via_index(unsigned int index) {
        this->data[index].client_sock_fd = -1;
    }
};