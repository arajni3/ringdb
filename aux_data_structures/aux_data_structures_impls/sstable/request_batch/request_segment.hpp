#pragma once
#include <cstring>
#include "../../../aux_data_structures_concepts/sstable/stack/stack.hpp"
#define MEMTABLE_SIZE (1 << (MEMTABLE_SIZE_MB_BITS + 20))


template<StackLike Stack, unsigned int stack_depth>
class RequestSegment {
    private:
    Stack stack;

    public:
    char* data;

    /* adapt the binary search-like algorithm used in the move_down_to_children in 
    lsm_tree.hpp to construct a balanced binary search tree from the sorted memtable array
    */
    void insert_memtable(char* memtable_data) {
        unsigned int current, left, right, left_child, right_child, tree_base;
        int back = 0;
        stack.push_top(MEMTABLE_SIZE >> 1, 0, MEMTABLE_SIZE, 0);
        data = new char[MEMTABLE_SIZE];
        std::memcpy(data, memtable_data + (MEMTABLE_SIZE >> 1), KEY_LENGTH + VALUE_LENGTH);
        unsigned int top_tree_index[stack_depth];
        top_tree_index[0] = 0;

        while (!stack.empty()) {
            auto top = stack.top();
            current = top[0];
            left = top[1];
            right = top[2];
            left_child = (left + current) >> 1;
            right_child = (current + right) >> 1;

            stack.pop_top();
            tree_base = top_tree_index[back--] << 1;

            if (left_child < current) {
                stack.push_top(left_child, left, current, 0);

                // left child of tree index i is 2i + 1
                top_tree_index[back] = tree_base + 1;

                std::memcpy(data + top_tree_index[back], memtable_data + left, 
                KEY_LENGTH + VALUE_LENGTH);
                ++back;
            }
            if (right_child > current) {
                stack.push_top(right_child, current, right, 0);

                // right child of tree index i is 2i + 2
                top_tree_index[back] = tree_base + 2;

                std::memcpy(data + top_tree_index[back], memtable_data + right, 
                KEY_LENGTH + VALUE_LENGTH);
                ++back;
            }
        }
    }
};