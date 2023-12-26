#pragma once
#include "concepts"
#include "../connection_request/connection_request.hpp"
#include "../../../enums/request_type.hpp"

/* Stores a page-size batch of connection requests. These requests are
copies of ones already in the connection pool rather than pointers
to connection pool entries for the sake of better spatial data locality. This data 
structure shall be implemented as a sorted array.
*/
template<typename ReadWritePool>
concept ReadWritePoolLike = requires(
    ReadWritePool rw_pool,
    char* socket_buffer,
    int client_sock_fd,
    unsigned int index, 
    void* req_batch_ref, 
    RequestType rw_type) {
        requires ConnectionRequestLike<std::remove_reference_t<decltype(*rw_pool.data)>>;
        requires std::same_as<unsigned int, decltype(rw_pool.length)>;
        requires std::same_as<unsigned int, decltype(rw_pool.num_sent)>;
        requires std::same_as<bool, decltype(rw_pool.present_in_level)>;
        {rw_pool.insert_read_write(socket_buffer, client_sock_fd, rw_type, req_batch_ref)} 
        -> std::same_as<void>;
        {rw_pool.find_index_starting_from(index, socket_buffer)} 
        -> std::same_as<int>;
        
        /* The remove function should simply change the client_sock_fd of the entry 
        to -1 to avoid shifting around or nulling out entries. The length of the pool 
        should never decrease.
        */
        {rw_pool.remove_via_index(index)} -> std::same_as<void>;
};