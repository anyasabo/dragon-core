#pragma once

// Host socket types. The test replaces socket() and sendto().
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define socket stub_socket
#define sendto stub_sendto
int stub_socket(int domain, int type, int protocol);
ssize_t stub_sendto(int fd, const void *buf, size_t len, int flags,
                    const struct sockaddr *to, socklen_t tolen);
