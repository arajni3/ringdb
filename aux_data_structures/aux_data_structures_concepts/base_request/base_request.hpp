#pragma once
#include <concepts>
#include "../../enums/request_type.hpp"

/* Using this base class, ConnectionRequest and RequestBatch will
have minimal padding with the appropriate ordering of fields. Moreover, the casting
can be done using static_cast and hence done at compile time and
thus will lead to no loss in performance.
*/
template<typename BaseRequest>
concept BaseRequestLike = requires(BaseRequest base_req) {
    requires std::same_as<RequestType, decltype(base_req.req_type)>;

    // RequestBatch child class should call this in its own constructor
    requires std::constructible_from<RequestType>;

    // use for connection requests in connection pool
    requires std::default_initializable<BaseRequest>;
};