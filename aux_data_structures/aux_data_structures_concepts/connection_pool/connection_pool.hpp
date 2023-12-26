#include <concepts>
#include "../sstable/request_batch/request_batch.hpp"
#include "connection_request/connection_request.hpp"
#include "../sstable/request_batch/request_batch.hpp"
#include "../../enums/request_type.hpp"

/* A pool of connection requests represented by a simple array. We can use a simple
array due to the following:

Since the server socket descriptor will be registered in the network thread's fixed
file table before client connections will come and since the fixed server socket
descriptor will be the only fixed file descriptor in the network thread before
client connections will come in, the direct client socket descriptors will be in the 
range [1, MAX_SIMULTANEOUS_CONNECTIONS]. We will store only the direct client socket
descriptors in the connection pool, so we can implement the connection pool as a 
simple array and map a direct descriptor to its array index in the connection pool by 
subtracting 1 from it. No tree, hash, or bucketing is needed.

The direct client socket descriptor should also be used to get a free socket buffer 
as the buffer ring data structure from io_uring cannot be used reliably to free up 
buffers for connection requests whose events occur out of order.
*/
template<typename ConnectionPool>
concept ConnectionPoolLike = requires(
    int client_sockfd,
    ConnectionPool connection_pool,
    RequestType req_type
    ) {
        requires ConnectionRequestLike<std::remove_reference_t<
        decltype(*connection_pool.connections)>>;
        requires std::same_as<char*, std::remove_reference_t<
        decltype(connection_pool.buffer_pool)>>;
        requires std::constructible_from<ConnectionPool, unsigned int, char*>;
        requires ConnectionRequestLike<std::remove_reference_t<
        decltype(*connection_pool.insert(client_sockfd, req_type))>>;
    };