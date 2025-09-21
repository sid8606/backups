// rdma_server.c
// RoCE server: registers a buffer, exchanges connection info with client, waits for RDMA_WRITE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <infiniband/verbs.h>

#define SIZE 4096
#define BACKLOG 1

struct conn_info {
    uint32_t qpn;
    uint32_t psn;
    uint32_t rkey;
    uint64_t vaddr;
    uint8_t gid[16];
};

// Exit on error
static void die(const char *s) {
    perror(s);
    exit(1);
}

// Get local GID for RoCE
static void get_local_gid(struct ibv_context *ctx, int port, int index, union ibv_gid *gid) {
    if (ibv_query_gid(ctx, port, index, gid))
        die("ibv_query_gid");
}

// Listen on TCP port and accept connection
int tcp_listen_and_accept(const char *port_str) {
    struct addrinfo hints = {0}, *res;
    int sock = -1, cl = -1;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port_str, &hints, &res) != 0) die("getaddrinfo");

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) die("socket");

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sock, res->ai_addr, res->ai_addrlen) < 0) die("bind");
    if (listen(sock, BACKLOG) < 0) die("listen");

    printf("Server listening on port %s ...\n", port_str);
    cl = accept(sock, NULL, NULL);
    if (cl < 0) die("accept");
    close(sock); // close listening socket
    freeaddrinfo(res);
    return cl;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    char *port_str = argv[1];

    // 1) Open RDMA device
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list) die("ibv_get_device_list");

    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    if (!ctx) die("ibv_open_device");

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd) die("ibv_alloc_pd");

    char *buf = malloc(SIZE);
    if (!buf) die("malloc");
    memset(buf, 0, SIZE);
    strcpy(buf, "INITIAL SERVER CONTENT");

    struct ibv_mr *mr = ibv_reg_mr(pd, buf, SIZE,
                    IBV_ACCESS_LOCAL_WRITE |
                    IBV_ACCESS_REMOTE_WRITE |
                    IBV_ACCESS_REMOTE_READ);
    if (!mr) die("ibv_reg_mr");

    struct ibv_cq *cq = ibv_create_cq(ctx, 10, NULL, NULL, 0);
    if (!cq) die("ibv_create_cq");

    struct ibv_qp_init_attr qp_init_attr = {0};
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr = 10;
    qp_init_attr.cap.max_recv_wr = 10;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) die("ibv_create_qp");

    // Move QP to INIT
    struct ibv_qp_attr attr = {0};
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = 1;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (ibv_modify_qp(qp, &attr,
                      IBV_QP_STATE |
                      IBV_QP_PKEY_INDEX |
                      IBV_QP_PORT |
                      IBV_QP_ACCESS_FLAGS)) die("ibv_modify_qp to INIT");

    // Prepare local connection info
    struct conn_info local = {0};
    local.qpn = qp->qp_num;
    local.psn = (uint32_t)(rand() & 0xffffff);
    local.rkey = mr->rkey;
    local.vaddr = (uintptr_t)buf;

    union ibv_gid gid;
    get_local_gid(ctx, 1, 0, &gid);
    memcpy(local.gid, gid.raw, 16);

    // 2) TCP listen and exchange connection info
    int cl = tcp_listen_and_accept(port_str);
    struct conn_info remote;
    if (write(cl, &local, sizeof(local)) != sizeof(local))
	    die("write local conn_info");
    if (read(cl, &remote, sizeof(remote)) != sizeof(remote))
	    die("read remote conn_info");

    printf("Exchanged connection info:\n");

    // Local info
    printf("Local QPN: %u\n", local.qpn);
    printf("Local PSN: %u\n", local.psn);
    printf("Local rkey: 0x%x\n", local.rkey);
    printf("Local lkey: 0x%x\n", mr->lkey);
    printf("Local vaddr: 0x%lx\n", (unsigned long)local.vaddr);
    printf("Local GID: ");
    for (int i = 0; i < 16; i++) printf("%02x", local.gid[i]);
    printf("\n\n");

    // Remote info
    printf("Remote QPN: %u\n", remote.qpn);
    printf("Remote PSN: %u\n", remote.psn);
    printf("Remote rkey: 0x%x\n", remote.rkey);
    printf("Remote vaddr: 0x%lx\n", (unsigned long)remote.vaddr);
    printf("Remote GID: ");
    for (int i = 0; i < 16; i++) printf("%02x", remote.gid[i]);
    printf("\n");


    // 3) Move QP to RTR
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_1024;
    attr.dest_qp_num = remote.qpn;
    attr.rq_psn = remote.psn;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.port_num = 1;
    memcpy(&attr.ah_attr.grh.dgid, remote.gid, 16);
    attr.ah_attr.grh.sgid_index = 0;
    attr.ah_attr.grh.hop_limit = 1;

    if (ibv_modify_qp(qp, &attr,
                      IBV_QP_STATE |
                      IBV_QP_AV |
                      IBV_QP_PATH_MTU |
                      IBV_QP_DEST_QPN |
                      IBV_QP_RQ_PSN |
                      IBV_QP_MIN_RNR_TIMER |
                      IBV_QP_MAX_DEST_RD_ATOMIC)) die("ibv_modify_qp to RTR");

    // 4) Move QP to RTS
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = local.psn;
    attr.max_rd_atomic = 1;
    if (ibv_modify_qp(qp, &attr,
                      IBV_QP_STATE |
                      IBV_QP_TIMEOUT |
                      IBV_QP_RETRY_CNT |
                      IBV_QP_RNR_RETRY |
                      IBV_QP_SQ_PSN |
                      IBV_QP_MAX_QP_RD_ATOMIC)) die("ibv_modify_qp to RTS");

    printf("QP moved to RTS. Waiting for client RDMA_WRITE...\n");

    // 5) Wait for client completion notification
    char donebuf[16];
    ssize_t r = read(cl, donebuf, sizeof(donebuf));
    if (r <= 0) fprintf(stderr, "Failed to read client done (r=%zd)\n", r);
    else {
        printf("Client signaled completion.\nServer buffer content (first 256 bytes):\n");
        fwrite(buf, 1, SIZE < 256 ? SIZE : 256, stdout);
        printf("\n");
    }

    // Cleanup
    close(cl);
    ibv_dereg_mr(mr);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    free(buf);

    return 0;
}

