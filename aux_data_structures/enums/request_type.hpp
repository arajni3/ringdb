#pragma once

typedef enum RequestType {
    ACCEPT,
    RECV,
    SEND,
    READ,
    WRITE,
    COMPACTION
} RequestType;