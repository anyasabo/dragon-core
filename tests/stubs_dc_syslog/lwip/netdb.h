#pragma once

// Host address types. The test replaces getaddrinfo() and freeaddrinfo().
#include <netdb.h>

#define getaddrinfo stub_getaddrinfo
#define freeaddrinfo stub_freeaddrinfo
int stub_getaddrinfo(const char *node, const char *service,
                     const struct addrinfo *hints, struct addrinfo **res);
void stub_freeaddrinfo(struct addrinfo *res);
