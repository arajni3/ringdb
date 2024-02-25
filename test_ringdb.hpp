#pragma once
#include <cstring>

#include <sys/socket.h>	
#include <arpa/inet.h>	
#include <unistd.h>
#include <cstdlib>
#include <algorithm>
#include <iostream>

#include "constinit_constants.hpp"

// usable when batch_num_requests size = 1
void test_ringdb(void* unused) {
    int num_recv = 0;
    while (num_recv < BATCH_NUM_REQUESTS) {
        int s;
        struct sockaddr_in6 addr;

        s = socket(AF_INET6, SOCK_STREAM, 0);
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(PORT);
        inet_pton(AF_INET6, "::1", &addr.sin6_addr);
        connect(s, (struct sockaddr *)&addr, sizeof(addr));

        char* buffer = new char[SOCKET_BUFFER_LENGTH];

        std::memcpy(buffer, "write", 5);
        std::fill(buffer + 5, buffer + 5 + KEY_LENGTH, 'a');
        std::fill(buffer + 5 + KEY_LENGTH, buffer + SOCKET_BUFFER_LENGTH, 'b');
        send(s, buffer, SOCKET_BUFFER_LENGTH, 0);
        close(s);

        s = socket(AF_INET6, SOCK_STREAM, 0);
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(PORT);
        inet_pton(AF_INET6, "::1", &addr.sin6_addr);
        connect(s, (struct sockaddr *)&addr, sizeof(addr));

        std::memcpy(buffer, "read\0", 5);
        char* desired_value = new char[VALUE_LENGTH];
        std::fill(desired_value, desired_value + VALUE_LENGTH, 'b');
        send(s, buffer, SOCKET_BUFFER_LENGTH, 0);
        recv(s, buffer, SOCKET_BUFFER_LENGTH, '\0');
        if (!memcmp(buffer + 5 + KEY_LENGTH, desired_value, VALUE_LENGTH)) {
            std::cout << "passed read test";
        } else {
            std::cout << "failed read test";
        }
        close(s);
        if (system("sudo rm -R /sstab1e") == -1) {
            std::cout << "\nFailed to delete test directory /sstab1e";
        }
    }
}