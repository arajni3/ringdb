#pragma once

struct CacheBufferEntry {
    char key[KEY_LENGTH];
    char value[VALUE_LENGTH];
};