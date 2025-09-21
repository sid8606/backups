#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>

#define BUFFER_SIZE 4096
#define TCP_PORT    18515   /* or any fixed port you want */

/* QP exchange info */
struct qp_info {
    uint32_t qpn;
    uint32_t psn;
    uint32_t rkey;
    uint64_t vaddr;
    union ibv_gid gid;
};

/* Simple error handler */
static inline void die(const char *msg) {
    fprintf(stderr, "Error: %s (%s)\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

#endif

