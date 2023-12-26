#pragma once
#include "../../enums/request_type.hpp"

struct BaseRequest {
    RequestType req_type;

    BaseRequest(RequestType req_type): req_type{req_type} {}

    BaseRequest() {}
};