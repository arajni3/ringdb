# RingDB
RingDB is a high-performance database based on the fast, highly scalable LSM tree architecture that uses the new, high-performance, asynchronous I/O library io_uring as its I/O backend, which is also RingDB's namesake. It uses the power of modern C++, especially C++20, for high performance, particularly by moving as many aspects of 
the program to the compile-time side as possible. It also ensures to maximize performance by 
optimizing branch prediction and data structure layout, splitting the logic into hot path and cold path, using multithreading and multiprocessing using zero-copy I/O, minimizing contention in shared data structures by speeding up critical section code and using lock-free algorithms, minimizing system calls, using a healthy number of worker threads, and using fast in-memory auxiliary data structures and algorithms to perform precise, effective bookkeeping and caching, and resource management which maximize batching and minimize I/O operations and maximize asynchrony when I/O is required. Finally, it uses CMake to neatly and efficiently build the project for both testing and production. 

The main contribution of RingDB is to show that a modern database architecture can be combined with new C++20 features and the new library io_uring, as well as traditional practices such as precise caching, optimizing branch prediction and data structure layout, splitting the logic into hot path and cold path, multithreading/processing, zero-copy I/O, lock-free data structures and maximally fast critical section algorithms, and minimizing system calls, along with the build system CMake, to produce a very scalable, high-performance database, which is in especially high demand in the modern age of the Internet and big data. The computing practices in RingDB are not limited to just databases, however; they span the entire field of high-performance computing (HPC), so other areas of HPC can be inspired by RingDB as well.

## Database Architecture
RingDB, at its core, is an LSM (log-structured merge) tree. The LSM tree was a response to the high latency experienced by expensive writes in B+ trees and their variants; performing writes in array-based trees, which are the only way to serialize trees to storage in an efficient manner, are extremely slow unlike rotations in pointer-based trees. 

The LSM tree fixes this by forcing the tree structures in storage to be immutable and in-memory structures to not be trees. The database is divided into multiple levels. Level 0 is locked in memory and consists of multiple lexicographically key-sorted arrays of key-value pairs called "memtables". Actual deletions are not performed, instead, deleted key-value pairs have a special value string called a tombstone, which, in RingDB, is just a string of null-terminating characters. When a memtable is filled, it is emptied and its contents are moved, or "compacted", to the next level in an array-based tree format to a file known as a "Sorted Strings Table" (SSTable). The SSTable is flushed to at most once and is never written to again. If there are no empty SSTables on the current level to flush the memtable to, the memtable is moved to the next level and so on and so forth until it finds one. The immutability of SSTables and the non-tree based structure of memtables ensures that writes are extremely fast, in fact non-flush writes happen only in memory, which are in sorted arrays, which are faster to write to than array-based trees. Also, LSM trees have less organizational overhead than B+ trees, which are designed for hard disk drives (HDDs), and thus are naturally faster on solid state drives (SSDs). However, reads can be slower since they may have to traverse multiple levels to find the most up-to-date values for their keys, and extra storage space is used since both keys and values are copied unlike in B+ trees, and tombstone values are used instead of actual deletions.

For each SSTable, reads are sped up by using a lightweight, immutable in-memory data structure called a Bloom Filter. A Bloom Filter is an efficient, probabilistic data structure with a configurable false positive probability that reports whether an input key is contained in the SSTable. False negatives never occur with bloom filters, but false positives may occur. They are also lighter-weight than Cuckoo Filters, which accomplish a similar purpose except they are configured for writes as well; since SSTables are immutable, bloom filters suffice. Additionally, each level has a small data structure called a sparse index, which is an array of pairs of keys where each pair represents the key boundary of an SSTable, which allows partitioning incoming requests into requests for each SSTable.

## io_uring in RingDB
io_uring is a new, high-performance asynchronous I/O kernel library that provides numerous resources for performing all forms of I/O in a fast, asynchronous manner, including sockets and files. In short, an io_uring session consists of an io_uring session file descriptor, used for all io_uring requests, and two in-memory circular queues, or rings, one for submissions and one for completions. Both of these circular queues can be accessed in a lock-free manner or using system calls. Both of these queues allow operations to be completed in batches, which minimizes the use of system calls and store barriers. Furthermore, io_uring allows an async worker thread option wherein a kernel thread listens for submissions to the submission queue in a lock-free manner and stays active as long as submissions come in frequently. With this option, RingDB, being a database, which is heavily I/O-bound, can submit requests without ever having to perform a system call, instead undergoing the much smaller overhead of NUMA; also, the lock-free completion queue check option does not use a system call either, so as long as the async worker thread is pinned to a CPU different from that of the requesting thread, which RingDB always does, RingDB can submit and process an arbitrary number of requests without ever performing a system call or suffering an extra context switch, undergoing at most the cost of NUMA, which is a huge boon for both icache and dcache hit rates, particularly L1 cache, as well as TLB hit rate.

io_uring, along with submission batching, also allows submission chaining, which enables the user to force certain io_uring events to not complete until other events have completed, which enables event-driven programming at the kernel-level and is thus very efficient because it naturally uses the kernel as the source of the truth for event handling and not naive user-space heuristics. io_uring has a convenient user-space library wrapper called liburing that is used throughout the RingDB codebase. In fact, RingDB is named after io_uring.

io_uring enables reducing overhead of traditional file descriptors with a feature called "registered" or "direct" file descriptors, which are array offsets that the user sets in an array of traditional file descriptors and passes to the kernel. From then on, the user can use the array offsets to refer to the file descriptors instead of the traditional file descriptors themselves; this prevents the need to use expensive atomic fgets calls that are used with traditional descriptors. RingDB uses these for its TCP/IPv6 server socket and connections as well as for its SSTable descriptors. RingDB also reduces overhead by using the single-thread optimization in io_uring, which allows further optimizations if it is guaranteed that an io_uring session will be accessed by only one application thread; io_uring sessions in RingDB are never shared between multiple threads, so they can enjoy these optimizations.

io_uring also supports managed buffer pools. When a user registers buffers to the kernel in a data structure called a buffer ring, io_uring caches the virtual-to-physical memory translations so that the translations do not need to be recomputed upon each I/O request involving those buffers. In simple I/O cases, they can improve performance. However, they currently have a rigid FIFO structure and thus cannot be used for networking. Also, to enable prefetching for efficient cache usage, RingDB must maintain buffer usage information in userspace, so registered buffers carry needless extra overhead. Moreover, they do not support removing buffers out of the box, hence they must be entirely unregistered and then reregistered with the remaining buffers to perform removal, whihc requires two system calls and an entire loop through the remaining buffer set. Hence, they are not used here. Instead, RingDB allocates memory-locked buffers with hugepages. The Linux kernel treats hugepages specially from a hugepage pool and does not shift around hugepage-allocated memory, so the memory translations of memory-locked hugepages remain fixed. This also enables DMA, which is needed for direct file I/O and zero-copy networking.

io_uring also allows sending data to different io_uring sessions in the same program using the io_uring_prep_msg_ring function, which sends input data to the second io_uring session's completion queue. With this feature, SSTable io_uring sessions can quickly send finished requests back to the network thread, which will then finish the associated client connection sessions. In fact, this, combined with the submission chaining feature and lock-free capability of completion queues both described previously, allows for io_uring to act as a natural I/O scheduler; each RingDB SSTable worker thread, which manages multiple SSTables at a time in a batch, can have its own io_uring session, and each SSTable, when it schedules an I/O operation, can chain its I/O requests to a message operation to its thread's session, and the latter message will appear in the thread's completion queue only after all of the SSTable's current I/O requests have finished; the message can contain an identifier for the SSTable that the thread can then use to schedule another I/O operation, enabling efficuent, effevtive, event-driven programming. All of this is done with minimal latency because, as said before, the event completion sequences are tied to the kernel, which is the best source of truth for I/O operations, not blind, naive user-space heuristics. This, combined with direct I/O for asynchronous, zero-copy file operations, hugely improves performance in RingDB.

Finally, io_uring has a built-in zero-copy send operation for socket I/O, which is more reliable for io_uring-based environments than the native MSG_ZEROCOPY option. This allows sending data over the network without creating intermediate copies of data in the kernel, a significant factor in performance that is used by many popular applications such as Confluent Kafka.

## Algorithms and Data Structures in RingDB
As a database, the most fundamental operations in RingDB are file I/O, which, along with the network I/O operations of its built-in TCP/IPv6 server, are also the slowest. Thus, numerous in-memory auxiliary data structures are used to perform bookkeeping to minimize the number of I/O operations and, when I/O is unavoidable, hide latency by maximizing asynchrony and batching. Of course, batching also helps to minimize I/O operations by allowing the same region(s) of the page cache to be hit by many requests at once.

First, work is separated using threads. The main thread handles Level 0 of the LSM tree. The network has its own thread, and the SSTables are partitioned into contiguous batches; each SSTable batch is managed by a worker thread and the number of worker threads is intended to be not too many to cause extra overhead but not too few to overload the existing worker threads. The network and main thread share the same io_uring async worker thread, but each SSTable worker thread has its own io_uring async worker thread shared amongst all its SSTable io_uring sessions. RingDB uses glibc to pin threads to single CPUs to maximize CPU caching and thereby maximize performance. All the io_uring async worker threads are pinned to CPU 1, the network thread is pinned to CPU 2, and the SSTable worker threads are pinned to the remaining CPUs by using their identification numbers and reducing them modulo the number of remaining CPUs; this allows to balance the SSTable worker threads fairly in a manner similar to consistent hashing in distributed systems. Pinning all io_uring threads to CPU 1 ensures that they will not interfere with the application threads of RingDB.

Due to usage of multiple threads, lock-free synchronization is a factor in performance, and due to the use of multiple CPUs, so is cache coherence. Throughout the RingDB project, data structures are designed to be as lock-free and contention-minimizing as possible. This is accomplished by using atomic integers combined with load-linked / store-conditional (LL/SC) operations over strong compare-and-swap (CAS). The former allows CAS to fail upon a context switch whereas the latter must complete even in the case of a context switch, so the latter has better responsiveness and less latency. This is useful on non-CISC CPU architectures, which support LL/SC (x86 is not one of them). Memory ordering is an important part of cache coherence. To protect critical sections, which is where most of the atomic code is, sequential consistency is required, though, in the initialization stage of an SSTable, a relaxed memory ordering can be used to increment the number of SSTables whose initialization is completed.

Along with cache coherence, false sharing must be eliminated to achieve top performance. To this end, all shared data structures, including request batches and client buffers, are padded to the CPU cache-line + prefetch size (this quantity is defined by a macro) to avoid false sharing.

To maximize the hit rate of the TLB and L1 cache as well as define a good request batch size, RingDB uses huge pages whenever the page size is greater than the standard 4KB (4096-byte) size. To enable direct I/O, each buffer is padded so that the next buffer starts at a multiple of the filesystem's block size, and each I/O read request's desired starting offset is rounded to the largest multiple of the block size less than or equal to the desired starting offset; moreover, each buffer's total length is itself a multiple of the block size.

The network thread contains a connection pool data structure that manages (direct) client socket descriptors and quickly assigns a buffer to each connection.

RingDB uses request batching by using the batching support of io_uring to combine multiple client request into single request batches. Each request batch contains an array of its requests sorted by key, which will be important later. A request batch structure is padded to avoid false sharing since it may be accessed by more than one thread in its lifetime. Each SSTable has a wait queue of request batches into which incoming request batches are inserted for processing. Since SSTables occur in contiguous batches among worker threads, only SSTables that live on the boundary between worker threads need store operations to access their request batch wait queues safely, so the SSTables that need store operations and the ones that don't are identified at the beginning of the RingDB program. This eliminates all unnecessary atomic store operations, which hugely improves performance.

When a request batch needs to move to the next level in the LSM tree, it first needs to access the sparse index and bloom filters of the next level. It uses LL/SC in a loop and then acquires the exclusive access if it needs to (along with SSTables, levels that are not multithreaded are also determined at the beginning of the program). Each level's information is packed into a LevelInfo data structure, which is already padded to avoid false sharing as it contains composed data structures, such as SSTable info data structures, that are padded to avoid false sharing. The bloom filters and sparse index are used to decompose the request batch into a request batch for each SSTable on the level. If the request batch is actually a compaction, it is simply moved on to the level after the next level. If the request batch is a batch of client requests, then requests that are definitely not on the next level (they are not in the sparse index's entire range or their SSTable as determined by the sparse index definitely does not contain the client keys according to the SSTable's bloom filter) are also moved to the level after the next level. If there is not another level, the request batch is simply deleted if it is a compaction, and unsatisfied requests are marked as unsatisfied and sent to the network thread for termination.

When a memtable needs to be compacted, it is quickly transformed into an array-based tree using preorder traversal. Because a memtable is a sorted array, the tree can be computed quickly and is balanced. It is immutable, so it does not need to waste memory by storing balance factors or colors. Because I/O is expensive, RingDB makes extensive and intelligent use of the page cache (RAM) to minimize I/O. Instead of using the kernel's page cache, it uses direct I/O via the O_DIRECT option when creating the traditional SSTable file descriptor. Additionally, since each SSTable is a tree structure, it contains enough page cache buffers to traverse the entire height of its tree, which minimizes the number of page cache invalidations required to complete a single request in a request batch.

Because SSTables are in tree format, client request batches are sorted by key so that they can be accessed in preorder traversal. This allows the keys that would be highest in the SSTable to be accessed first in the SSTable, which allows the highest tree levels to be cached, which maximizes the cache hit rate. Tree traversal in memory is implemented using a custom, in-memory, array-based stack, which has minimal overhead compared to recursion or pointer-based stacks and is usable because the height of a memtable is limited. Additionally, once a client request is finished, its offset in the SSTable is saved for its child requests so that its child requests can start searching from that offset instead of at the top each time, which hugely reduces the number of read operations required for future client requests in the batch. Moreover, the SSTable data cached in memory is not wiped out when client request batches finish, so future client batches can potentially use the cached data from the beginning and thereby experience a further speedup. Furthermore, when an SSTable schedules a read request, it also schedules a second read request, if an extra page cache buffer is available, to prefetch extra data similar to how a CPU prefetches one or more extra cache lines, so that it can cache more data at once and reduce the number of separate I/O read request operations. 

Each SSTable has a cache helper data structure. The cache helper internally uses a data structure called a sparse buffer index, named after the sparse index in the traditional LSM tree architecture. The sparse buffer index maps SSTable offset boundary pairs to page cache buffers to quickly locate a buffer when the SSTable needs be read starting from a certain offset. The sparse buffer index is also a sorted array because it allows for fast binary searches and insertions are faster than array-based trees. The sparse buffer index also contains a secondary array mapping the ordinal number of a buffer to its location in the sorted array to allow for quick removals, which will be needed to maximize page cache usage as discussed later.

Each SSTable worker thread cycles amongst all its SSTables, performing what is effectively an event loop, and in each iteration of the loop, it checks its own completion queue for completed SSTable I/O operation events. If it finds one, it determines the SSTable associated with the message, then determines the type of operation, and schedules the next necessary action with that SSTable. Then, in the same iteration of the loop, it checks the current SSTable in the loop and schedules the necessary actions. The SSTable worker thread does this in a completely asynchronous, lock-free manner using the lock-free support in io_uring for checking completion queues. If an SSTable has no request batches in its request batch wait queue, the SSTable worker thread waits another loop cycle to check again. If the SSTable again does not have any incoming requests, the SSTable is considered inactive and the program transfers a fraction of the SSTable's buffers to the previous level if the current level is not 1 (memtables do not need these page cache buffers). This allows for maximum-throughput usage of the page cache since inactive SSTables will not be holding buffers for no reason. 

To transfer page cache buffers, each SSTable has a buffer queue, which, like the level info and request batch wait queue data structures, is also determined to be single- or multi-threaded at the beginning of the program. Transferring buffers from an inactive SSTable upwards is simple, but when an SSTable needs to route a request batch downwards to the next level, it needs to transfer buffers using a complex algorithm so that the next level can use buffers if they do not have enough. The downstream buffer transfer algorithm uses the request batch decomposition to transfer buffers to each of the next level's SSTables proportionally based on the size of the latter SSTable's new request batch size and does not transfer more than that. When removing buffers in general, the SSTable uses the auxiliary array in its cache helper's sparse buffer index to remove buffers. Like the level info, the request batch, and the request batch wait queue, the buffer queue is a shared data structure and hence is aligned (padded) to avoid false sharing.

All long-living auxiliary data structures along with memtables, except for request batches (though this can be changed), are locked into memory to provide maximum speed by minimizing page faults.

Wherever possible, branch predictions are eliminated using bitwise operators or identified to be likely or unlikely to be taken based on the logical control flow of the application and optimized using the C++20 [[likely]] and [[unlikely]] attributes, respectively, to optimize the generated assembly for conditional branches. In fact, this is done extra to improve throughput over latency as a large-scale database should prioritize throughput over latency, so generally the hot path is optimized over the cold path in branch predictions.

## C++20 and CMake in RingDB
RingDB makes extensive use of C++20 for modularity, safety, and especially performance. RingDB uses C++20 concepts as compile-time interfaces over pure virtual abstract classes. The former are completely compile-time, guaranteed, and hence do not bring any overhead at run-time, while the latter rely on run-time polymorphism and may only possibly, not definitely, be compiled away by compiler optimizations. As with pure virtual abstract classes, concepts help to abstract away the exact implementation of the auxiliary data structure from its usage.

To further move things to compile time, RingDB also combines C++17 inline constexpr, which allows for global constants, with C++20 constinit, which forces variables to be defined at compile-time, to allow for computed global compile-time constants; these compile-time constants will be replaced by literal constants at run-time and can be defined using static inline constinit const. Because static inline constinit const allows for global constants that are additionally guaranteed compile-time, they can be used as template parameters so that the sizes of containers in auxiliary data structures can be known at compile-time, which allows for data structures to be composed in other data structures directly and not stored indirectly as pointers. This hugely improves performance because it greatly increases cache locality since data structures are not stored locally, space is not wasted because extra pointers are not stored, and heap contention is hugely reduced because related data are contiguous and not in disparate areas of heap memory contending with heap writes by other threads. Moreover, shared data structures that contain substructures additionally save on space because all the substructures are composed directly and are thus contiguous, and hence only the master data structure needs to be aligned (padded) to avoid false sharing, not every single substructure needs to be aligned since it will not be in a disparate area of heap memory.

RingDB minimizes the time spent in critical sections using contiguously allocated structs composed contiguously in other structs (particularly with bloom filters in the LevelInfo data structure using const{expr/eval}-computed template constants) as well as carefully scheduling heap deallocations to not occur inside critical sections when they can occur outside (for example, not deleting the old head node of an SSTable's request batch wait queue when reading until after exiting the critical path). In particular, RingDB uses OpenMP parallelism in bloom filter operations to increase the speed of bloom filters by several orders of magnitude, which is crucial for high performance as LevelInfo critical sections are a major bottleneck in performance and are worth optimizing even at slight cost of extra context switching for other threads not involved in LevelInfo critical sections.

As stated previously, to optimize the generated assembly for conditional branches, RingDB uses the C++20 [[likely]] and [[unlikely]] attributes on conditional branches based on whether they are likely or unlikely, respectively, in the logical control flow of the application.

RingDB additionally uses other aspects of modern C++, such as C++20 consteval for a compile-time log base 2 function (the built-in one from cmath will not be constexpr until C++26) as well as consteval wrapper functions for the bloom filter code using the multitude of constexpr math functions from the convenient third-party gcem library, std::atomic for atomics and std::thread for multithreading from C++11, returning direct constructor calls for guaranteed copy elision in C++17, and a particularly important one, std::launder from C++17 to use an array of bytes read from storage as an array of bytes of an object marked as defined behavior and thus without suffering adverse compiler optimizations due to assumed undefined behavior.

RingDB uses CMake to build the project using Unix Makefiles and supports both debug and release builds. It contains a script build_ringdb.sh for building RingDB along with liburing as a static library. Note that RingDB requires root privileges to have enough concurrent connections and use enough page cache as it is intended to be a large, primary database. The compiler used in this project is currently gcc/g++, but this project does not actually use any gcc-specific features other than glibc, which is supported by most major compilers such as clang, so that option can be changed in the build_ringdb.sh file. When RingDB is built, the executable, whose name will simply be "RingDB", will be located in the ${cmake_config} of the RingDB project directory, and it can be executed via ./RingDB or simply RingDB and it will run with root privileges.

## Platform Requirements
io_uring is a Linux kernel library, and RingDB uses other Linux-specific properties such as file paths, so RingDB can be run only on Linux. Furthermore, it requires Linux 6.1 at the minimum since it uses aspects of io_uring which run only on Linux 6.1 or later. This should not be an issue for Windows users since WSL2 already supports Linux 6.1. A further performance improvement related to registered io_uring session descriptors in io_uring_register*-like calls can be experienced from Linux 6.3 and onwards. Naturally, the compiler must support C++20 and glibc. The gcem library is header-only and can be built via CMake as done in the build_ringdb script. Finally, the minimum CMake version required is listed in the CMakeLists.txt file.

## Testing
RingDB uses an extra macro RINGDB_TEST along with CMake support to support testing. The test mode has its own macros separate from the development/production-mode macros. Currently, the test is contained in the test_ringdb.hpp file, which supports sending from only one client and it is usable only when the number of client requests in a request batch is 1. To test with multiple clients, one can spin up a Docker container for each request and have each container share a named pipe with the main application and pipe the results of the requests to the main application's standard output.
