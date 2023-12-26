#include <concepts>
#include "../level_info/sparse_index/sparse_index.hpp"

// Sorted array locked in memory.
template<typename MemTable>
concept MemTableLike = requires(MemTable mem_table, char* key, char* value) {
    requires std::same_as<char[(1 << MEMTABLE_SIZE_MB_BITS) * (1 << 20)], 
    decltype(mem_table.data)>;
    
    /* number of bytes written to the data buffer, not simply the number of 
    key-value pairs
    */
    requires std::same_as<unsigned int, decltype(mem_table.size)>;

    /* Write the value to the value pointer offset in the socket buffer
    if the key is in the memtable and return true for success or false
    if the key is not in the memtable.
    */
    {mem_table.read(key, value)} -> std::same_as<bool>;

    // fill the whole data buffer with null-terminating characters
    {mem_table.empty_out()} -> std::same_as<void>;
};

template<typename MemTable, typename SparseIndex>
concept MemTableWrite = requires(MemTable mem_table, unsigned int table_num, 
SparseIndex& memtable_sparse_index, char* key, char* value) {
    requires MemTableLike<decltype(mem_table)>;
    requires SparseIndexLike<std::remove_reference_t<decltype(memtable_sparse_index)>>;
    /* Change the sparse index if necessary.
    */
    {mem_table.write(key, value, table_num, memtable_sparse_index)} -> std::same_as<void>;
};