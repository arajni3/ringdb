#pragma once
#include <concepts>
#include "request_segment.hpp"
#include "../../base_request/base_request.hpp"
#include "../../connection_pool/connection_request/connection_request.hpp"
#include "../../connection_pool/readwrite_pool/readwrite_pool.hpp"
#include "../../../enums/request_type.hpp"

/* Unlike connection requests, we will not have a preallocated pool of read/write
request batches because (1) unlike connection requests, such request batches may
break up into multiple request batches containing less elements and so finding a free
request batch pool entry for each such request batch without a simple, lasting unique 
identifier as in the connection pool will create time and cache inefficiencies,
(2) Linux, since version 4.14, minimizes syscall overhead by avoiding automatic TLB 
flushes by using process context identifiers (PCIDs) which allow threads of the same 
process to use the same TLB entries and not flush the TLB upon thread context switch
and which prevent the need to flush the TLB on context switching to the kernel since
only TLB entries that need to be accessed but which have a different PCID will be
invalidated (the chance of occurrence of which is minimized with address space layout
randomization (ASLR)), and PCID invalidation is ultimately done at hardware level 
rather than in software, (3) since multiple threads will be creating read or write 
request batches (sstables will not create socket-based write batches), the request 
batch pool will have to be a shared data structure. For these three reasons, heap 
allocations and deallocations may minimal overhead compared to the overhead required 
for maintaining a read/write request batch pool, though there is still a little
overhead involved in the syscall due to having to save the CPU register values
and executing instructions in kernel space instead of in the user applications's code
segment.

However, dynamically allocating request batches can cause heap contention given the
fact that we will be using a large amount of RAM anyway. Also, if we align each
request batch to avoid false sharing, then, since disjoint request batches share no
client socket descriptors in common and since client socket descriptors will not 
get reused until they are first closed, different threads can access different 
entries of the request batch pool at the same time without causing false sharing.
We can have the identifier of a request batch be its smallest client socket descriptor
and have a method for its next-smallest client socket descriptor. Since no other
request batch will have the next-smallest client socket descriptor even after the 
smallest one is closed, the request batch array pool entry corresponding to the 
next-smallest client socket descriptor will be free to use if the number of entries
in the request batch pool is MAX_NUM_CONNECTIONS. However, allocating these many 
read/write request batches will utilize a lot of extra RAM.

The RequestBatch data structure should be aligned to avoid false sharing anyway since even 
in the non-pooled case, we will be deleting arbitrary request batches in arbitrary memory 
ranges and the data fields of the readwrite pool and request segment will be heap-allocated 
only some time after the request batch will be created.
*/
template<typename RequestBatch>
concept RequestBatchLike = requires(RequestBatch req_batch, int client_sockfd, char* buffer, 
RequestType req_type) {
    /* these two fields should be packed into a union called "content" to take up the same space; 
    the union must be named in order for these two constraints to not cause a compiler error
    */
    requires ReadWritePoolLike<decltype(req_batch.content.readwrite_pool)>;
    requires RequestSegmentLike<decltype(req_batch.content.req_seg)>;

    /* need global request type READ to differentiate from compactions and to denote completions 
    in the network thread
    */
    requires std::constructible_from<RequestBatch, RequestType>;

    requires std::destructible<RequestBatch>;

    /* should internally call readwrite_pool.insert_read_write(buffer, client_sockfd, req_type) 
    giving the this pointer statically casted to void* as the request batch reference
    */
    {req_batch.insert_read_write(buffer, client_sockfd, req_type)} -> std::same_as<void>;
};


template<typename RequestBatch, typename BaseRequest>
concept RequestBatchReq = std::derived_from<RequestBatch, BaseRequest>;