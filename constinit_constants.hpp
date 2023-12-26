#pragma once
#include <numeric>
#include <fcntl.h>

/* compile time-evaluated ceil of log base 2 function in place of standard log2 function, which, 
along with the other log functions from cmath, will not be constexpr until C++26
*/
template<double doub>
consteval unsigned int my_log2_ceil() {
    unsigned int power = 1, exponent = 0;
    while (power < doub) {
        power <<= 1;
        ++exponent;
    }
    return exponent;
}

template<double doub>
consteval unsigned int my_log2_floor() {
    unsigned int exponent = my_log2_ceil<doub>();
    return exponent - ((1 << exponent) > doub);
}


#define NUM_SSTABLES (TOTAL_SSTABLE_STORAGE_SPACE_MB / (1 << MEMTABLE_SIZE_MB_BITS))

/* Structure of a TCP message will be 
request_type + key + value with no spaces in-between,
where the request type bit is "read\0" (equals "read") for reads and 
"write" for writes
*/
#define SOCKET_BUFFER_LENGTH (5 + KEY_LENGTH + VALUE_LENGTH)

/* Number of requests in a batch of client requests.
*/
#define BATCH_NUM_REQUESTS (PAGE_SIZE / SOCKET_BUFFER_LENGTH)

// In C++17, can define global constants using inline const{expr/init} (init since C++20)

// also the fair number of page cache buffers per sstable
static inline const constinit unsigned int max_sstable_height = MEMTABLE_SIZE_MB_BITS * 20;

static inline const constinit unsigned int stack_depth = my_log2_ceil<
static_cast<double>((1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20) / (KEY_LENGTH + VALUE_LENGTH))>();

static inline const constinit unsigned int max_buffer_ring_size = max_sstable_height * 
LEVEL_FACTOR;

/* the ceiling of the log (base 10) of of 2**32 is 10, and unsigned integers are tightly bounded 
from above by 2**32 - 1 hence can be tightly represented in base 10 by ten digits, so ten digits 
are needed to serialize a number in human (base 10) format into file names
*/
static inline const constinit unsigned int level_str_len = 10;
static inline const constinit unsigned int sstable_number_str_len = 10;

static inline const constinit unsigned int fair_sstable_page_cache_size = 
TOTAL_SSTABLE_PAGE_CACHE_SIZE / NUM_SSTABLES;

/* Usable page cache buffer size will not be the actual buffer size
to avoid false sharing, and it must be a multiple of the cache buffer 
entry size as well as the block size.
*/
static inline const constinit unsigned int fair_aligned_sstable_page_cache_buffer_size = 
std::lcm(std::lcm(static_cast<unsigned int>(fair_sstable_page_cache_size 
/ max_sstable_height), KEY_LENGTH + VALUE_LENGTH), BLOCK_SIZE);

/* will be a multiple of the block size since the fair aligned sstable
page cache buffer size will be a multiple of the block size, and will also
avoid false sharing since it will pad at least a positive multiple of the
minimum no-false-sharing size to the fair aligned sstable page cache buffer
size (the actual buffer size used for I/O will be the latter buffer size)
*/
static inline const constinit unsigned int fair_unaligned_sstable_page_cache_buffer_size = 
fair_aligned_sstable_page_cache_buffer_size + std::lcm(
    BLOCK_SIZE,
    ALIGN_NO_FALSE_SHARING
);

/* Everything including this program should be root-owned, which should already 
be able to read and write to anything. When an sstable file is first created, 
it will be owned by the user who created it, which will be root, so this mode 
value shouldn't change any permissions.
*/
static inline const constinit mode_t mode = S_IRWXU;