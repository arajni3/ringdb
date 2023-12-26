#pragma once
#include <concepts>
#include "../../base_request/base_request.hpp"
#include "../../sstable/request_batch/request_batch.hpp"
#include "../../level_info/buffer_queue/buffer_queue.hpp"

template<typename ConnectionRequest>
concept ConnectionRequestLike = requires(ConnectionRequest conn_req) {
    // Will be statically casted to RequestBatchRef* whenever used
    requires std::same_as<void*, decltype(conn_req.req_batch_ref)>;

    requires std::same_as<int, decltype(conn_req.client_sock_fd)>;
    requires std::same_as<char*, decltype(conn_req.buffer)>;
    requires std::default_initializable<ConnectionRequest>;
};

template<typename ConnectionRequest, typename BaseRequest>
concept ConnectionRequestReq = std::derived_from<ConnectionRequest, BaseRequest>;