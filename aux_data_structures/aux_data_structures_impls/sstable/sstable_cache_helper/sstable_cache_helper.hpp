#pragma once
#include "../../../aux_data_structures_concepts/sstable/sstable_cache_helper/sparse_buffer_index/sparse_buffer_index.hpp"
#include "../../../aux_data_structures_concepts/sstable/sstable_cache_helper/cache_location.hpp"

template<SparseBufferIndexLike SparseBufferIndex, CacheLocationLike CacheLocation>
struct SSTableCacheHelper {
    // must be public in order for compiler to not complain about associated concept constraint
    SparseBufferIndex sparse_buffer_index;

    unsigned int free_buffers_left, id_of_least_recently_selected_buffer;

    SSTableCacheHelper(): free_buffers_left{0} {
        /* converts to the maximum value of std::size_t since the latter is an unsigned type and 
        arithmetic is performed in two's complement notation
        */
        sparse_buffer_index.cur_min_invalid_offset = -1;
        
        sparse_buffer_index.id_of_most_recently_selected_buffer = 0;
        id_of_least_recently_selected_buffer = 0;
    }

    void set_cur_min_invalid_offset(std::size_t invalid_sstable_offset) {
        sparse_buffer_index.cur_min_invalid_offset = invalid_sstable_offset;
    }

    std::size_t get_cur_min_invalid_offset() {
        return sparse_buffer_index.cur_min_invalid_offset;
    }

    void replenish_least_recently_selected_buffer() {
        ++free_buffers_left;
        unsigned int sparse_buffer_index_location = sparse_buffer_index
        .sparse_buffer_index_locations[id_of_least_recently_selected_buffer++];
        sparse_buffer_index.cache_locations[sparse_buffer_index_location].buffer_id = -1;
    }

    CacheLocation get_buffer_info(std::size_t desired_sstable_offset) {
        unsigned int left = 0, right = sparse_buffer_index.id_of_most_recently_selected_buffer + 1;
        unsigned int mid;
        while (left < right) {
            mid = (left + right) >> 1;
            if (sparse_buffer_index.cache_locations[mid].sstable_offset_boundary[1] 
            < desired_sstable_offset) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        if (left == sparse_buffer_index.id_of_most_recently_selected_buffer + 1) {
            return CacheLocation(-1, -1, 0);
        }
        CacheLocation location = sparse_buffer_index.cache_locations[left];
        return (location.sstable_offset_boundary[0] <= desired_sstable_offset 
        && location.sstable_offset_boundary[1] >= desired_sstable_offset && 
        location.buffer_id != -1) ? location : CacheLocation(-1, -1, 0);
    }

    void add_buffer() {
        ++free_buffers_left;
    }

    void map_buffer_to_offset_boundary(unsigned int buffer_id, 
    std::size_t sstable_offset1, std::size_t sstable_offset2) {
        unsigned int left = 0, right = sparse_buffer_index.id_of_most_recently_selected_buffer + 1;
        unsigned int mid;
        while (left < right) {
            mid = (left + right) >> 1;
            if (sparse_buffer_index.cache_locations[mid]
            .sstable_offset_boundary[1] < sstable_offset1) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }

        CacheLocation* location = sparse_buffer_index.cache_locations + left;
        if (left == sparse_buffer_index.id_of_most_recently_selected_buffer) {
            location->sstable_offset_boundary[0] = sstable_offset1;
            location->sstable_offset_boundary[1] = sstable_offset2;
            location->buffer_id = buffer_id;
        } else {
            CacheLocation* location2, *location3;
            location2->sstable_offset_boundary[0] = sstable_offset1;
            location2->sstable_offset_boundary[1] = sstable_offset2;
            location2->buffer_id = buffer_id;

            for (unsigned int i = left; 
            i < sparse_buffer_index.id_of_most_recently_selected_buffer; ++i, ++location) {
                location3->sstable_offset_boundary[0] = location->sstable_offset_boundary[0];
                location3->sstable_offset_boundary[1] = location->sstable_offset_boundary[1];
                location3->buffer_id = location->buffer_id;

                location->sstable_offset_boundary[0] = location2->sstable_offset_boundary[0];
                location->sstable_offset_boundary[1] = location2->sstable_offset_boundary[1];
                location->buffer_id = location2->buffer_id;

                location2->sstable_offset_boundary[0] = location3->sstable_offset_boundary[0];
                location2->sstable_offset_boundary[1] = location3->sstable_offset_boundary[1];
                location2->buffer_id = location3->buffer_id;
            }
            location->sstable_offset_boundary[0] = location2->sstable_offset_boundary[0];
            location->sstable_offset_boundary[1] = location2->sstable_offset_boundary[1];
            location->buffer_id = location2->buffer_id;
        }

        ++sparse_buffer_index.id_of_most_recently_selected_buffer;
        sparse_buffer_index.sparse_buffer_index_locations[buffer_id] = left;
        --free_buffers_left;
    }

    void remove_buffer_range(unsigned int buffer_id1, unsigned int buffer_id2) {
        free_buffers_left -= buffer_id2 - buffer_id1;

        if (buffer_id1 <= sparse_buffer_index.id_of_most_recently_selected_buffer) {
            unsigned int old_id_most_recently_selected = sparse_buffer_index.
            id_of_most_recently_selected_buffer;
            /* buffer_id1 will never be 0 (because the buffer transfer-out algorithm ensures that 
            it will never take all the buffers, so buffer_id1 - 1 will never overflow)
            */
            sparse_buffer_index.id_of_most_recently_selected_buffer = buffer_id1 - 1;
            unsigned int sparse_buffer_index_location;

            for (unsigned int buffer_id = std::max(buffer_id1, 
            id_of_least_recently_selected_buffer); buffer_id <= old_id_most_recently_selected; 
            ++buffer_id) {
                sparse_buffer_index_location = sparse_buffer_index.sparse_buffer_index_locations[
                buffer_id];
                sparse_buffer_index.cache_locations[sparse_buffer_index_location].buffer_id = -1;
            }
            // all the cached data has been transferred out due to buffer transfers
            if (id_of_least_recently_selected_buffer > buffer_id1) {
                id_of_least_recently_selected_buffer = 0;
                sparse_buffer_index.id_of_most_recently_selected_buffer = 0;
            }
        }
    }

    unsigned int get_id_of_most_recently_selected_buffer() {
        return sparse_buffer_index.id_of_most_recently_selected_buffer;
    }
};