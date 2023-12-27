// taken from https://github.com/barrust/bloom/ and edited for performance and convenience
#ifndef BARRUST_BLOOM_FILTER_H__
#define BARRUST_BLOOM_FILTER_H__
/*******************************************************************************
***
***     Author: Tyler Barrus
***     email:  barrust@gmail.com
***
***     Version: 1.9.0
***     Purpose: Simple, yet effective, bloom filter implementation
***
***     License: MIT 2015
***
***     URL: https://github.com/barrust/bloom
***
*******************************************************************************/

#include <inttypes.h>       /* PRIu64 */
#include <cstddef>
#include <cstdio>
#include <numeric>
#include <cmath>           /* pow, exp */
#include <cstring>         /* strlen */
#include <sys/mman.h>       /* mmap, mummap */

#include <gcem.hpp>

/* https://gcc.gnu.org/onlinedocs/gcc/Alternate-Keywords.html#Alternate-Keywords */
#ifndef __GNUC__
#define __inline__ inline
#endif

#ifdef __APPLE__
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#define BLOOMFILTER_VERSION "1.8.2"
#define BLOOMFILTER_MAJOR 1
#define BLOOMFILTER_MINOR 8
#define BLOOMFILTER_REVISION 2

#define BLOOM_SUCCESS 0
#define BLOOM_FAILURE -1

#define bloom_filter_get_version()    (BLOOMFILTER_VERSION)

#define CHECK_BIT_CHAR(c, k)  ((c) & (1 << (k)))
#define CHECK_BIT(A, k)       (CHECK_BIT_CHAR(A[((k) / 8)], ((k) % 8)))
// #define set_bit(A,k)          (A[((k) / 8)] |=  (1 << ((k) % 8)))
// #define clear_bit(A,k)        (A[((k) / 8)] &= ~(1 << ((k) % 8)))

/* define some constant magic looking numbers */
#define CHAR_LEN KEY_LENGTH
#define LOG_TWO_SQUARED  0.480453013918201388143813800   // 0.4804530143737792968750000
                                                 
                                                         // 0.4804530143737792968750000
#define LOG_TWO 0.693147180559945286226764000

/* https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetTable */
#define B2(n) n,     n+1,     n+1,     n+2
#define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
static const unsigned char bits_set_table[256] = {B6(0), B6(1), B6(1), B6(2)};

typedef uint64_t* (*BloomHashFunction) (int num_hashes, const char *str);

consteval uint64_t compile_time_num_bits(uint64_t num_elems, double false_pos_prob) {
    return gcem::ceil((static_cast<int64_t>(-num_elems) * gcem::log(false_pos_prob))
    / LOG_TWO_SQUARED);
}

consteval unsigned int compile_time_num_hashes(uint64_t num_elems, double false_pos_prob) {
    uint64_t m = compile_time_num_bits(num_elems, false_pos_prob);
    unsigned int k = gcem::round(LOG_TWO * m / num_elems);
    return k;
}

consteval unsigned long compile_time_bloom_length(uint64_t num_elems, double false_pos_prob) {
    uint64_t m = compile_time_num_bits(num_elems, false_pos_prob);
    return gcem::ceil(m / (CHAR_LEN * 1.0));
}

template<uint64_t num_elements, double false_pos_prob>
struct BloomFilter {
    uint64_t estimated_elements = num_elements;
    double false_positive_probability = false_pos_prob;
    unsigned int number_hashes = compile_time_num_hashes(num_elements, false_pos_prob);
    uint64_t number_bits = compile_time_num_bits(num_elements, false_pos_prob);
    /* bloom filter */
    unsigned char bloom[
        std::lcm(compile_time_bloom_length(num_elements, false_pos_prob), ALIGN_NO_FALSE_SHARING)
    ];;
    unsigned long bloom_length = std::lcm(
        compile_time_bloom_length(num_elements, false_pos_prob), ALIGN_NO_FALSE_SHARING);
    uint64_t elements_added = 0;

    BloomHashFunction hash_function;
    BloomFilter() {
        bloom_filter_set_hash_function(this, nullptr);
    }
};

/*  Initialize a standard bloom filter in memory; this will provide 'optimal' size and hash numbers.

    Estimated elements is 0 < x <= UINT64_MAX.
    False positive rate is 0.0 < x < 1.0 */

/* Set or change the hashing function */
template<uint64_t num_elements, double false_pos_prob>
constexpr void bloom_filter_set_hash_function(BloomFilter<num_elements, false_pos_prob> *bf, BloomHashFunction hash_function);

/* Release all memory used by the bloom filter */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_destroy(BloomFilter<num_elements, false_pos_prob> *bf);

/* reset filter to unused state */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_clear(BloomFilter<num_elements, false_pos_prob> *bf);

/* Add a string (or element) to the bloom filter */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_add_string(BloomFilter<num_elements, false_pos_prob> *bf, const char *str);

/* Add a string to a bloom filter using the defined hashes */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_add_string_alt(BloomFilter<num_elements, false_pos_prob> *bf, uint64_t *hashes, unsigned int number_hashes_passed);

/* Check to see if a string (or element) is or is not in the bloom filter */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_check_string(BloomFilter<num_elements, false_pos_prob> *bf, const char *str);

/* Check if a string is in the bloom filter using the passed hashes */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_check_string_alt(BloomFilter<num_elements, false_pos_prob> *bf, uint64_t *hashes, unsigned int number_hashes_passed);

/* Calculates the current false positive rate based on the number of inserted elements */
template<uint64_t num_elements, double false_pos_prob>
double bloom_filter_current_false_positive_rate(BloomFilter<num_elements, false_pos_prob> *bf);

/* Count the number of bits set to 1 */
template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_count_set_bits(BloomFilter<num_elements, false_pos_prob> *bf);

/*  Estimate the number of unique elements in a Bloom Filter instead of using the overall count
    https://en.wikipedia.org/wiki/Bloom_filter#Approximating_the_number_of_items_in_a_Bloom_filter
    m = bits in Bloom filter
    k = number hashes
    X = count of flipped bits in filter */
template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_estimate_elements(BloomFilter<num_elements, false_pos_prob> *bf);
uint64_t bloom_filter_estimate_elements_by_values(uint64_t m, uint64_t X, int k);

/* Wrapper to set the inserted elements count to the estimated elements calculation */
template<uint64_t num_elements, double false_pos_prob>
void bloom_filter_set_elements_to_estimated(BloomFilter<num_elements, false_pos_prob> *bf);

/*  Generate the desired number of hashes for the provided string
    NOTE: It is up to the caller to free the allocated memory */
template<uint64_t num_elements, double false_pos_prob>
uint64_t* bloom_filter_calculate_hashes(BloomFilter<num_elements, false_pos_prob> *bf, const char *str, unsigned int number_hashes);


/*******************************************************************************
    Merging, Intersection, and Jaccard Index Functions
    NOTE: Requires that the bloom filters be of the same type: hash, estimated
    elements, etc.
*******************************************************************************/

/* Merge Bloom Filters - inserts information into res */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_union(BloomFilter<num_elements, false_pos_prob> *res, BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2);
template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_count_union_bits_set(BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2);

/*  Find the intersection of Bloom Filters - insert into res with the intersection
    The number of inserted elements is updated to the estimated elements calculation */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_intersect(BloomFilter<num_elements, false_pos_prob> *res, BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2);
template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_count_intersection_bits_set(BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2);

/*  Calculate the Jacccard Index of the Bloom Filters
    NOTE: The closer to 1 the index, the closer in bloom filters. If it is 1, then
    the Bloom Filters contain the same elements, 0.5 would mean about 1/2 the same
    elements are in common. 0 would mean the Bloom Filters are completely different. */
template<uint64_t num_elements, double false_pos_prob>
double bloom_filter_jaccard_index(BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2);


/*******************************************************************************
***
***     Author: Tyler Barrus
***     email:  barrust@gmail.com
***
***     Version: 1.9.0
***
***     License: MIT 2015
***
*******************************************************************************


/*******************************************************************************
***  PRIVATE FUNCTIONS
*******************************************************************************/
static uint64_t* __default_hash(int num_hashes, const char *str);
static uint64_t __fnv_1a(const char *key, int seed);
template<uint64_t num_elements, double false_pos_prob>
static void __calculate_optimal_hashes(BloomFilter<num_elements, false_pos_prob> *bf);
static int __sum_bits_set_char(unsigned char c);
template<uint64_t num_elements, double false_pos_prob>
static int __check_if_union_or_intersection_ok(BloomFilter<num_elements, false_pos_prob> *res, BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2);

template<uint64_t num_elements, double false_pos_prob>
constexpr void bloom_filter_set_hash_function(BloomFilter<num_elements, false_pos_prob> *bf, BloomHashFunction hash_function) {
    bf->hash_function = (hash_function == nullptr) ? __default_hash : hash_function;
}

template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_destroy(BloomFilter<num_elements, false_pos_prob> *bf) {
    bf->bloom = nullptr;
    bf->elements_added = 0;
    bf->estimated_elements = 0;
    bf->false_positive_probability = 0;
    bf->number_hashes = 0;
    bf->number_bits = 0;
    bf->hash_function = nullptr;
    return BLOOM_SUCCESS;
}

template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_clear(BloomFilter<num_elements, false_pos_prob> *bf) {
    for (unsigned long i = 0; i < bf->bloom_length; ++i) {
        bf->bloom[i] = 0;
    }
    bf->elements_added = 0;
    return BLOOM_SUCCESS;
}

template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_add_string(BloomFilter<num_elements, false_pos_prob> *bf, const char *str) {
    uint64_t *hashes = bloom_filter_calculate_hashes(bf, str, bf->number_hashes);
    int res = bloom_filter_add_string_alt(bf, hashes, bf->number_hashes);
    free(hashes);
    return res;
}

template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_check_string(BloomFilter<num_elements, false_pos_prob> *bf, const char *str) {
    uint64_t *hashes = bloom_filter_calculate_hashes(bf, str, bf->number_hashes);
    int res = bloom_filter_check_string_alt(bf, hashes, bf->number_hashes);
    free(hashes);
    return res;
}

template<uint64_t num_elements, double false_pos_prob>
uint64_t* bloom_filter_calculate_hashes(BloomFilter<num_elements, false_pos_prob> *bf, const char *str, unsigned int number_hashes) {
    return bf->hash_function(number_hashes, str);
}

/* Add a string to a bloom filter using the defined hashes */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_add_string_alt(BloomFilter<num_elements, false_pos_prob> *bf, uint64_t *hashes, unsigned int number_hashes_passed) {
    if (number_hashes_passed < bf->number_hashes) {
        std::fprintf(stderr, "Error: not enough hashes passed in to correctly check!\n");
        return BLOOM_FAILURE;
    }

    for (unsigned int i = 0; i < bf->number_hashes; ++i) {
        unsigned long idx = (hashes[i] % bf->number_bits) / 8;
        int bit = (hashes[i] % bf->number_bits) % 8;

        #pragma omp atomic update
        bf->bloom[idx] |= (1 << bit); // set the bit
    }

    #pragma omp atomic update
    bf->elements_added++;
    return BLOOM_SUCCESS;
}

/* Check if a string is in the bloom filter using the passed hashes */
template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_check_string_alt(BloomFilter<num_elements, false_pos_prob> *bf, uint64_t *hashes, unsigned int number_hashes_passed) {
    if (number_hashes_passed < bf->number_hashes) {
        std::fprintf(stderr, "Error: not enough hashes passed in to correctly check!\n");
        return BLOOM_FAILURE;
    }

    unsigned int i;
    int r = BLOOM_SUCCESS;
    for (i = 0; i < bf->number_hashes; ++i) {
        int tmp_check = CHECK_BIT(bf->bloom, (hashes[i] % bf->number_bits));
        if (tmp_check == 0) {
            r = BLOOM_FAILURE;
            break; // no need to continue checking
        }
    }
    return r;
}

template<uint64_t num_elements, double false_pos_prob>
double bloom_filter_current_false_positive_rate(BloomFilter<num_elements, false_pos_prob> *bf) {
    int num = bf->number_hashes * bf->elements_added;
    double d = -num / (double) bf->number_bits;
    double e = exp(d);
    return pow((1 - e), bf->number_hashes);
}

template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_count_set_bits(BloomFilter<num_elements, false_pos_prob> *bf) {
    uint64_t i, res = 0;
    for (i = 0; i < bf->bloom_length; ++i) {
        res += __sum_bits_set_char(bf->bloom[i]);
    }
    return res;
}

template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_estimate_elements(BloomFilter<num_elements, false_pos_prob> *bf) {
    return bloom_filter_estimate_elements_by_values(bf->number_bits, bloom_filter_count_set_bits(bf), bf->number_hashes);
}

uint64_t bloom_filter_estimate_elements_by_values(uint64_t m, uint64_t X, int k) {
    /* m = number bits; X = count of flipped bits; k = number hashes */
    double log_n = log(1 - ((double) X / (double) m));
    return (uint64_t)-(((double) m / k) * log_n);
}

template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_union(BloomFilter<num_elements, false_pos_prob> *res, BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2) {
    // Ensure the bloom filters can be unioned
    if (__check_if_union_or_intersection_ok(res, bf1, bf2) == BLOOM_FAILURE) {
        return BLOOM_FAILURE;
    }
    uint64_t i;
    for (i = 0; i < bf1->bloom_length; ++i) {
        res->bloom[i] = bf1->bloom[i] | bf2->bloom[i];
    }
    bloom_filter_set_elements_to_estimated(res);
    return BLOOM_SUCCESS;
}

template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_count_union_bits_set(BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2) {
    // Ensure the bloom filters can be unioned
    if (__check_if_union_or_intersection_ok(bf1, bf1, bf2) == BLOOM_FAILURE) {  // use bf1 as res
        return BLOOM_FAILURE;
    }
    uint64_t i, res = 0;
    for (i = 0; i < bf1->bloom_length; ++i) {
        res += __sum_bits_set_char(bf1->bloom[i] | bf2->bloom[i]);
    }
    return res;
}

template<uint64_t num_elements, double false_pos_prob>
int bloom_filter_intersect(BloomFilter<num_elements, false_pos_prob> *res, BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2) {
    // Ensure the bloom filters can be used in an intersection
    if (__check_if_union_or_intersection_ok(res, bf1, bf2) == BLOOM_FAILURE) {
        return BLOOM_FAILURE;
    }
    uint64_t i;
    for (i = 0; i < bf1->bloom_length; ++i) {
        res->bloom[i] = bf1->bloom[i] & bf2->bloom[i];
    }
    bloom_filter_set_elements_to_estimated(res);
    return BLOOM_SUCCESS;
}

template<uint64_t num_elements, double false_pos_prob>
void bloom_filter_set_elements_to_estimated(BloomFilter<num_elements, false_pos_prob> *bf) {
    bf->elements_added = bloom_filter_estimate_elements(bf);
}

template<uint64_t num_elements, double false_pos_prob>
uint64_t bloom_filter_count_intersection_bits_set(BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2) {
    // Ensure the bloom filters can be used in an intersection
    if (__check_if_union_or_intersection_ok(bf1, bf1, bf2) == BLOOM_FAILURE) {  // use bf1 as res
        return BLOOM_FAILURE;
    }
    uint64_t i, res = 0;
    for (i = 0; i < bf1->bloom_length; ++i) {
        res += __sum_bits_set_char(bf1->bloom[i] & bf2->bloom[i]);
    }
    return res;
}

template<uint64_t num_elements, double false_pos_prob>
double bloom_filter_jaccard_index(BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2) {
    // Ensure the bloom filters can be used in an intersection and union
    if (__check_if_union_or_intersection_ok(bf1, bf1, bf2) == BLOOM_FAILURE) {  // use bf1 as res
        return (double)BLOOM_FAILURE;
    }
    double set_union_bits = (double)bloom_filter_count_union_bits_set(bf1, bf2);
    if (set_union_bits == 0) {  // check for divide by 0 error
        return 1.0; // they must be both empty for this to occur and are therefore the same
    }
    return (double)bloom_filter_count_intersection_bits_set(bf1, bf2) / set_union_bits;
}

/*******************************************************************************
*    PRIVATE FUNCTIONS
*******************************************************************************/
template<uint64_t num_elements, double false_pos_prob>
static void __calculate_optimal_hashes(BloomFilter<num_elements, false_pos_prob> *bf) {
    // calc optimized values
    long n = bf->estimated_elements;
    double p = bf->false_positive_probability;
    uint64_t m = ceil((-n * logl(p)) / LOG_TWO_SQUARED);  // AKA pow(log(2), 2);
    unsigned int k = round(LOG_TWO * m / n);             // AKA log(2.0);
    // set paramenters
    bf->number_hashes = k; // should check to make sure it is at least 1...
    bf->number_bits = m;
    long num_pos = ceil(m / (CHAR_LEN * 1.0));
    bf->bloom_length = num_pos;
}

static int __sum_bits_set_char(unsigned char c) {
    return bits_set_table[c];
}

template<uint64_t num_elements, double false_pos_prob>
static int __check_if_union_or_intersection_ok(BloomFilter<num_elements, false_pos_prob> *res, BloomFilter<num_elements, false_pos_prob> *bf1, BloomFilter<num_elements, false_pos_prob> *bf2) {
    if (res->number_hashes != bf1->number_hashes || bf1->number_hashes != bf2->number_hashes) {
        return BLOOM_FAILURE;
    } else if (res->number_bits != bf1->number_bits || bf1->number_bits != bf2->number_bits) {
        return BLOOM_FAILURE;
    } else if (res->hash_function != bf1->hash_function || bf1->hash_function != bf2->hash_function) {
        return BLOOM_FAILURE;
    }
    return BLOOM_SUCCESS;
}

/* NOTE: The caller will free the results */
static uint64_t* __default_hash(int num_hashes, const char *str) {
    uint64_t *results = (uint64_t*)calloc(num_hashes, sizeof(uint64_t));
    mlock(results, sizeof(uint64_t) * num_hashes);
    int i;
    for (i = 0; i < num_hashes; ++i) {
        results[i] = __fnv_1a(str, i);
    }
    return results;
}

static uint64_t __fnv_1a(const char *key, int seed) {
    // FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
    int i, len = strlen(key);
    uint64_t h = 14695981039346656037ULL + (31 * seed); // FNV_OFFSET 64 bit with magic number seed
    for (i = 0; i < len; ++i){
            h = h ^ (unsigned char) key[i];
            h = h * 1099511628211ULL; // FNV_PRIME 64 bit
    }
    return h;
}
#endif /* END BLOOM FILTER HEADER */
