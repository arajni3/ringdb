#pragma once
#include <sys/mman.h>
#include "../../aux_data_structures_concepts/connection_pool/connection_request/connection_request.hpp"
#include "../../enums/request_type.hpp"

template<ConnectionRequestLike ConnectionRequest>
struct ConnectionPool {
    /* must be public fields so that compiler does not complain about associated concept 
    constraints
    */
    ConnectionRequest* connections;
    char* buffer_pool;

    ConnectionPool(unsigned int num_connections, char* buffer_pool) {
        this->buffer_pool = buffer_pool;
        connections = new ConnectionRequest[num_connections];
        mlock(this->connections, sizeof(ConnectionRequest) * num_connections);
    }

    ConnectionRequest* insert(int client_sockfd, RequestType req_type) {
        ConnectionRequest* conn_req = this->connections + (client_sockfd - 1);
        conn_req->client_sock_fd = client_sockfd;
        conn_req->buffer = this->buffer_pool + (client_sockfd - 1);
        conn_req->req_type = req_type;

        return conn_req;
    }
};