#pragma once
#include <concepts>

template<typename Filter>
concept FilterLike = requires(Filter filter, char* key) {
    {filter.insert_key(key)} -> std::same_as<void>;
    {filter.delete_key(key)} -> std::same_as<void>;
    {filter.prob_contains_key(key)} -> std::same_as<bool>;
    requires std::default_initializable<Filter>;
};