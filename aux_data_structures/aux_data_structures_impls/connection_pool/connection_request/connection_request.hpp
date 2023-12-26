#pragma once
#include "../../../aux_data_structures_concepts/base_request/base_request.hpp"

template<BaseRequestLike BaseRequest>
struct ConnectionRequest : public BaseRequest {
    void* req_batch_ref;
    int client_sock_fd;
    char* buffer;
};