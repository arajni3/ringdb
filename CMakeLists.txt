cmake_minimum_required(VERSION 3.22.1)
project(RingDB VERSION 1.0.0)

#[[ this macro should be defined along with the following CMake option set on 
whenever you want to be in test mode, and otherwise this macro not be defined and the 
corresponding CMake option should be turned off
]]
add_compile_definitions(RINGDB_TEST)
# associated CMake option flag denoting test mode
option(TEST_RINGDB "Test Mode" ON)

if (TEST_RINGDB)  
    # TEST MACROS

    add_compile_definitions(PORT=8001)

    add_compile_definitions(MEMTABLE_SIZE_MB_BITS=1)

    add_compile_definitions(TOTAL_SSTABLE_STORAGE_SPACE_MB=1000)

    add_compile_definitions(LEVEL_FACTOR=10)

    add_compile_definitions(NUM_SSTABLE_WORKER_THREADS=200)

    add_compile_definitions(FILTER_FALSE_POS_PROB=0.01)
    add_compile_definitions(KEY_LENGTH=1)
    add_compile_definitions(VALUE_LENGTH=2)

    add_compile_definitions(PAGE_SIZE=4096)

    add_compile_definitions(BLOCK_SIZE=4096)

    add_compile_definitions(ALIGN_NO_FALSE_SHARING=64)

    add_compile_definitions(MAX_SIMULTANEOUS_CONNECTIONS=100)

    add_compile_definitions(NUM_PROCESSORS=8)

    add_compile_definitions(TOTAL_SSTABLE_PAGE_CACHE_SIZE=1000)

else()
    # PRODUCTION/DEVELOPMENT MACROS
    add_compile_definitions(PORT=8001)

    # provide the number of bits of the memtable size, which should be a power of 2
    add_compile_definitions(MEMTABLE_SIZE_MB_BITS=1)

    # must provide value in terms of megabytes
    add_compile_definitions(TOTAL_SSTABLE_STORAGE_SPACE_MB=1000)

    # level factor should divide the total sstable storage space
    add_compile_definitions(LEVEL_FACTOR=10)

    #[[ number of sstable worker threads should divide the number of sstables for an even 
    worker thread load and should be at least 200 for large workloads (expected for a 
    database)
    ]]
    add_compile_definitions(NUM_SSTABLE_WORKER_THREADS=200)

    add_compile_definitions(FILTER_FALSE_POS_PROB=0.01)
    add_compile_definitions(KEY_LENGTH=1)
    add_compile_definitions(VALUE_LENGTH=2)

    #[[ Default page size on the host platform may be larger than 4096 depending on 
    administrator settings. In general, using huge pages will improve performance by 
    causing more TLB hits, fewer page faults, and better batching in the LSM tree 
    application, which uses the PAGE_SIZE macro to determine batch request sizes. If the 
    PAGE_SIZE macro is greater than 4096, then the LSM tree application will use the 
    default hugepage size via MAP_HUGETLB in mmap. The default hugepage size can be 
    configured by the administrator according to the mmap man page.
    ]]
    add_compile_definitions(PAGE_SIZE=4096)

    #[[ The block size of the filesystem can be changed by the administrator. In general, 
    larger block sizes work better with larger storages since they enable more data to be 
    pulled into memory at a time.
    ]]
    add_compile_definitions(BLOCK_SIZE=4096)

    #[[ Depending on some CPU architectures, may need to align to more than one 
    cache line to avoid false sharing for prefetched blocks, e.g., 
    64 * 2 = 128 bytes. Also, the cache line size may not be 64 bytes, though it is on 
    most CPU architectures.
    ]]
    add_compile_definitions(ALIGN_NO_FALSE_SHARING=64)

    add_compile_definitions(MAX_SIMULTANEOUS_CONNECTIONS=100)

    #[[ for best performance, change this value to use as many CPU cores as possible such 
    that there are as few SSTable worker threads for each of the last NUM_PROCESSORS - 3 as
    possible
    ]]
    add_compile_definitions(NUM_PROCESSORS=8)

    #[[ Be aware that the actual amount of page cache used for the sstables will
    be slightly different from the amount listed here. This value should be provided in 
    bytes. 
    ]]
    add_compile_definitions(TOTAL_SSTABLE_PAGE_CACHE_SIZE=1000)

endif()


set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/liburing/src/include)
include_directories(${CMAKE_SOURCE_DIR}/gcem/include)

# activate OpenMP extensions
if (MSVC)
    # warning level 4
    add_compile_options(/openmp)
else()
    # additional warnings
    add_compile_options(-fopenmp)
endif()

add_executable(RingDB main.cpp)
target_link_libraries(RingDB Threads::Threads)
target_link_libraries(RingDB ${CMAKE_SOURCE_DIR}/liburing/src/liburing.a)
target_compile_features(RingDB PUBLIC cxx_std_20)



set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
