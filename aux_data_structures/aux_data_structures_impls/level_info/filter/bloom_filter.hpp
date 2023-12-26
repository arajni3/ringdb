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

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>       /* PRIu64 */
#include <cstddef>
#include <numeric>
#include <cstdio>
#include <stdlib.h>
#include <math.h>           /* pow, exp */
#include <stdio.h>          /* printf */
#include <string.h>         /* strlen */
#include <fcntl.h>          /* O_RDWR */
#include <sys/mman.h>       /* mmap, mummap */
#include <sys/types.h>      /* */
#include <sys/stat.h>       /* fstat */
#include <unistd.h>         /* close */

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

typedef uint64_t* (*BloomHashFunction) (int num_hashes, const char *str);

typedef struct bloom_filter {
    /* bloom parameters */
    uint64_t estimated_elements;
    float false_positive_probability;
    unsigned int number_hashes;
    uint64_t number_bits;
    /* bloom filter */
    unsigned char *bloom;
    unsigned long bloom_length;
    uint64_t elements_added;
    BloomHashFunction hash_function;
} BloomFilter;


/*  Initialize a standard bloom filter in memory; this will provide 'optimal' size and hash numbers.

    Estimated elements is 0 < x <= UINT64_MAX.
    False positive rate is 0.0 < x < 1.0 */
int bloom_filter_init_alt(BloomFilter *bf, uint64_t estimated_elements, float false_positive_rate, BloomHashFunction hash_function);
static __inline__ int bloom_filter_init(BloomFilter *bf, uint64_t estimated_elements, float false_positive_rate) {
    return bloom_filter_init_alt(bf, estimated_elements, false_positive_rate, NULL);
}

/*  Export and import as a hex string; not space effecient but allows for storing
    multiple blooms in a single file or in a database, etc.

    NOTE: It is up to the caller to free the allocated memory */
char* bloom_filter_export_hex_string(BloomFilter *bf);
int bloom_filter_import_hex_string_alt(BloomFilter *bf, const char *hex, BloomHashFunction hash_function);
static __inline__ int bloom_filter_import_hex_string(BloomFilter *bf, char *hex) {
    return bloom_filter_import_hex_string_alt(bf, hex, NULL);
}

/* Set or change the hashing function */
void bloom_filter_set_hash_function(BloomFilter *bf, BloomHashFunction hash_function);

/* Release all memory used by the bloom filter */
int bloom_filter_destroy(BloomFilter *bf);

/* reset filter to unused state */
int bloom_filter_clear(BloomFilter *bf);

/* Add a string (or element) to the bloom filter */
int bloom_filter_add_string(BloomFilter *bf, const char *str);

/* Add a string to a bloom filter using the defined hashes */
int bloom_filter_add_string_alt(BloomFilter *bf, uint64_t *hashes, unsigned int number_hashes_passed);

/* Check to see if a string (or element) is or is not in the bloom filter */
int bloom_filter_check_string(BloomFilter *bf, const char *str);

/* Check if a string is in the bloom filter using the passed hashes */
int bloom_filter_check_string_alt(BloomFilter *bf, uint64_t *hashes, unsigned int number_hashes_passed);

/* Calculates the current false positive rate based on the number of inserted elements */
float bloom_filter_current_false_positive_rate(BloomFilter *bf);

/* Count the number of bits set to 1 */
uint64_t bloom_filter_count_set_bits(BloomFilter *bf);

/*  Estimate the number of unique elements in a Bloom Filter instead of using the overall count
    https://en.wikipedia.org/wiki/Bloom_filter#Approximating_the_number_of_items_in_a_Bloom_filter
    m = bits in Bloom filter
    k = number hashes
    X = count of flipped bits in filter */
uint64_t bloom_filter_estimate_elements(BloomFilter *bf);
uint64_t bloom_filter_estimate_elements_by_values(uint64_t m, uint64_t X, int k);

/* Wrapper to set the inserted elements count to the estimated elements calculation */
void bloom_filter_set_elements_to_estimated(BloomFilter *bf);

/*  Generate the desired number of hashes for the provided string
    NOTE: It is up to the caller to free the allocated memory */
uint64_t* bloom_filter_calculate_hashes(BloomFilter *bf, const char *str, unsigned int number_hashes);

/* Calculate the size the bloom filter will take on disk when exported in bytes */
uint64_t bloom_filter_export_size(BloomFilter *bf);

/*******************************************************************************
    Merging, Intersection, and Jaccard Index Functions
    NOTE: Requires that the bloom filters be of the same type: hash, estimated
    elements, etc.
*******************************************************************************/

/* Merge Bloom Filters - inserts information into res */
int bloom_filter_union(BloomFilter *res, BloomFilter *bf1, BloomFilter *bf2);
uint64_t bloom_filter_count_union_bits_set(BloomFilter *bf1, BloomFilter *bf2);

/*  Find the intersection of Bloom Filters - insert into res with the intersection
    The number of inserted elements is updated to the estimated elements calculation */
int bloom_filter_intersect(BloomFilter *res, BloomFilter *bf1, BloomFilter *bf2);
uint64_t bloom_filter_count_intersection_bits_set(BloomFilter *bf1, BloomFilter *bf2);

/*  Calculate the Jacccard Index of the Bloom Filters
    NOTE: The closer to 1 the index, the closer in bloom filters. If it is 1, then
    the Bloom Filters contain the same elements, 0.5 would mean about 1/2 the same
    elements are in common. 0 would mean the Bloom Filters are completely different. */
float bloom_filter_jaccard_index(BloomFilter *bf1, BloomFilter *bf2);


#ifdef __cplusplus
} // extern "C"
#endif


/*******************************************************************************
***
***     Author: Tyler Barrus
***     email:  barrust@gmail.com
***
***     Version: 1.9.0
***
***     License: MIT 2015
***
*******************************************************************************/


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


/*******************************************************************************
***  PRIVATE FUNCTIONS
*******************************************************************************/
static uint64_t* __default_hash(int num_hashes, const char *str);
static uint64_t __fnv_1a(const char *key, int seed);
static void __calculate_optimal_hashes(BloomFilter *bf);
static int __sum_bits_set_char(unsigned char c);
static int __check_if_union_or_intersection_ok(BloomFilter *res, BloomFilter *bf1, BloomFilter *bf2);


int bloom_filter_init_alt(BloomFilter *bf, uint64_t estimated_elements, float false_positive_rate, BloomHashFunction hash_function) {
    if(estimated_elements == 0 || estimated_elements > UINT64_MAX || false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
        return BLOOM_FAILURE;
    }
    bf->estimated_elements = estimated_elements;
    bf->false_positive_probability = false_positive_rate;
    __calculate_optimal_hashes(bf);
    // additionally pad to avoid false sharing
    bf->bloom = (unsigned char*)calloc(
        std::lcm(bf->bloom_length + 1, ALIGN_NO_FALSE_SHARING), sizeof(char)); // pad to ensure no running off the end
    mlock(bf->bloom, sizeof(char) * (bf->bloom_length + 1));

    bf->elements_added = 0;
    bloom_filter_set_hash_function(bf, hash_function);
    return BLOOM_SUCCESS;
}

void bloom_filter_set_hash_function(BloomFilter *bf, BloomHashFunction hash_function) {
    bf->hash_function = (hash_function == NULL) ? __default_hash : hash_function;
}

int bloom_filter_destroy(BloomFilter *bf) {
    bf->bloom = NULL;
    bf->elements_added = 0;
    bf->estimated_elements = 0;
    bf->false_positive_probability = 0;
    bf->number_hashes = 0;
    bf->number_bits = 0;
    bf->hash_function = NULL;
    return BLOOM_SUCCESS;
}

int bloom_filter_clear(BloomFilter *bf) {
    for (unsigned long i = 0; i < bf->bloom_length; ++i) {
        bf->bloom[i] = 0;
    }
    bf->elements_added = 0;
    return BLOOM_SUCCESS;
}

int bloom_filter_add_string(BloomFilter *bf, const char *str) {
    uint64_t *hashes = bloom_filter_calculate_hashes(bf, str, bf->number_hashes);
    int res = bloom_filter_add_string_alt(bf, hashes, bf->number_hashes);
    free(hashes);
    return res;
}


int bloom_filter_check_string(BloomFilter *bf, const char *str) {
    uint64_t *hashes = bloom_filter_calculate_hashes(bf, str, bf->number_hashes);
    int res = bloom_filter_check_string_alt(bf, hashes, bf->number_hashes);
    free(hashes);
    return res;
}

uint64_t* bloom_filter_calculate_hashes(BloomFilter *bf, const char *str, unsigned int number_hashes) {
    return bf->hash_function(number_hashes, str);
}

/* Add a string to a bloom filter using the defined hashes */
int bloom_filter_add_string_alt(BloomFilter *bf, uint64_t *hashes, unsigned int number_hashes_passed) {
    if (number_hashes_passed < bf->number_hashes) {
        fprintf(stderr, "Error: not enough hashes passed in to correctly check!\n");
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
int bloom_filter_check_string_alt(BloomFilter *bf, uint64_t *hashes, unsigned int number_hashes_passed) {
    if (number_hashes_passed < bf->number_hashes) {
        fprintf(stderr, "Error: not enough hashes passed in to correctly check!\n");
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

float bloom_filter_current_false_positive_rate(BloomFilter *bf) {
    int num = bf->number_hashes * bf->elements_added;
    double d = -num / (float) bf->number_bits;
    double e = exp(d);
    return pow((1 - e), bf->number_hashes);
}

char* bloom_filter_export_hex_string(BloomFilter *bf) {
    uint64_t i, bytes = sizeof(uint64_t) * 2 + sizeof(float) + (bf->bloom_length);
    char* hex = (char*)calloc((bytes * 2 + 1), sizeof(char));
    for (i = 0; i < bf->bloom_length; ++i) {
        sprintf(hex + (i * 2), "%02x", bf->bloom[i]); // not the fastest way, but works
    }
    i = bf->bloom_length * 2;
    sprintf(hex + i, "%016" PRIx64 "", bf->estimated_elements);
    i += 16; // 8 bytes * 2 for hex
    sprintf(hex + i, "%016" PRIx64 "", bf->elements_added);

    unsigned int ui;
    memcpy(&ui, &bf->false_positive_probability, sizeof (ui));
    i += 16; // 8 bytes * 2 for hex
    sprintf(hex + i, "%08x", ui);
    return hex;
}

int bloom_filter_import_hex_string_alt(BloomFilter *bf, const char *hex, BloomHashFunction hash_function) {
    uint64_t len = strlen(hex);
    if (len % 2 != 0) {
        fprintf(stderr, "Unable to parse; exiting\n");
        return BLOOM_FAILURE;
    }
    char fpr[9] = {0};
    char est_els[17] = {0};
    char ins_els[17] = {0};
    memcpy(fpr, hex + (len - 8), 8);
    memcpy(ins_els, hex + (len - 24), 16);
    memcpy(est_els, hex + (len - 40), 16);
    uint32_t t_fpr;

    bf->estimated_elements = strtoull(est_els, NULL, 16);
    bf->elements_added = strtoull(ins_els, NULL, 16);
    sscanf(fpr, "%x", &t_fpr);
    float f;
    memcpy(&f, &t_fpr, sizeof(float));
    bf->false_positive_probability = f;
    bloom_filter_set_hash_function(bf, hash_function);

    __calculate_optimal_hashes(bf);
    bf->bloom = (unsigned char*)calloc(bf->bloom_length + 1, sizeof(char));  // pad

    uint64_t i;
    for (i = 0; i < bf->bloom_length; ++i) {
        sscanf(hex + (i * 2), "%2hx", (short unsigned int*)&bf->bloom[i]);
    }
    return BLOOM_SUCCESS;
}

uint64_t bloom_filter_export_size(BloomFilter *bf) {
    return (uint64_t)(bf->bloom_length * sizeof(unsigned char)) + (2 * sizeof(uint64_t)) + sizeof(float);
}

uint64_t bloom_filter_count_set_bits(BloomFilter *bf) {
    uint64_t i, res = 0;
    for (i = 0; i < bf->bloom_length; ++i) {
        res += __sum_bits_set_char(bf->bloom[i]);
    }
    return res;
}

uint64_t bloom_filter_estimate_elements(BloomFilter *bf) {
    return bloom_filter_estimate_elements_by_values(bf->number_bits, bloom_filter_count_set_bits(bf), bf->number_hashes);
}

uint64_t bloom_filter_estimate_elements_by_values(uint64_t m, uint64_t X, int k) {
    /* m = number bits; X = count of flipped bits; k = number hashes */
    double log_n = log(1 - ((double) X / (double) m));
    return (uint64_t)-(((double) m / k) * log_n);
}

int bloom_filter_union(BloomFilter *res, BloomFilter *bf1, BloomFilter *bf2) {
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

uint64_t bloom_filter_count_union_bits_set(BloomFilter *bf1, BloomFilter *bf2) {
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

int bloom_filter_intersect(BloomFilter *res, BloomFilter *bf1, BloomFilter *bf2) {
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

void bloom_filter_set_elements_to_estimated(BloomFilter *bf) {
    bf->elements_added = bloom_filter_estimate_elements(bf);
}

uint64_t bloom_filter_count_intersection_bits_set(BloomFilter *bf1, BloomFilter *bf2) {
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

float bloom_filter_jaccard_index(BloomFilter *bf1, BloomFilter *bf2) {
    // Ensure the bloom filters can be used in an intersection and union
    if (__check_if_union_or_intersection_ok(bf1, bf1, bf2) == BLOOM_FAILURE) {  // use bf1 as res
        return (float)BLOOM_FAILURE;
    }
    float set_union_bits = (float)bloom_filter_count_union_bits_set(bf1, bf2);
    if (set_union_bits == 0) {  // check for divide by 0 error
        return 1.0; // they must be both empty for this to occur and are therefore the same
    }
    return (float)bloom_filter_count_intersection_bits_set(bf1, bf2) / set_union_bits;
}

/*******************************************************************************
*    PRIVATE FUNCTIONS
*******************************************************************************/
static void __calculate_optimal_hashes(BloomFilter *bf) {
    // calc optimized values
    long n = bf->estimated_elements;
    float p = bf->false_positive_probability;
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

static int __check_if_union_or_intersection_ok(BloomFilter *res, BloomFilter *bf1, BloomFilter *bf2) {
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
