#pragma once
#include <concepts>

/* An unsorted array of pairs of keys where each pair denotes the boundary
of a memtable at level zero or sstable at deeper levels. The underlying 
array is unsorted because the LEVEL_FACTOR value should be small; its 
default value is 10.
*/
template<typename SparseIndex>
concept SparseIndexLike = requires(
    SparseIndex sparse_index, 
    char* key,
    char* min_key, 
    char* max_key,
    unsigned int table_num
    ) {
        requires std::same_as<char[(KEY_LENGTH << 1) * LEVEL_FACTOR], 
        decltype(sparse_index.index)>;
        {sparse_index.insert_range(min_key, max_key,
        table_num)} -> std::same_as<void>;

        // inserts the key too at the {mem/ss}table index if necessary
        {sparse_index.get_table_num(key)} -> std::same_as<unsigned int>;

        {sparse_index.get_table_min_key(table_num)} -> std::same_as<char*>;
        {sparse_index.get_table_max_key(table_num)} -> std::same_as<char*>;
        {sparse_index.set_table_min_key(table_num, min_key)} -> std::same_as<void>;
        {sparse_index.set_table_max_key(table_num, max_key)} -> std::same_as<void>;

        {sparse_index.remove_table(table_num)} -> std::same_as<void>;
        requires std::default_initializable<SparseIndex>;
};