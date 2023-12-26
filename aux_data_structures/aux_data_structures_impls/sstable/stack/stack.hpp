#pragma once

template<unsigned int length>
class Stack {
    private:
    unsigned int back = 0;

    public:
    /* must be public in order for compiler to not complain about failure of corresponding
    constraint
    */
    unsigned int data[length][4];

    void push_top(unsigned int desired_sstable_offset, unsigned int cur_request, 
    unsigned int left, unsigned int right) {
        data[back][0] = desired_sstable_offset;
        data[back][1] = cur_request;
        data[back][2] = left;
        data[back++][3] = right;
    }

    auto& top() {
        return data[back];
    }

    void pop_top() {
        --back;
    }

    bool empty() {
        return back == 0;
    }
};