// rdma_client.c
// RoCE client: exchanges connection info with server and performs RDMA_WRITE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <infiniband/verbs.h>

/*
 * QP State Machine and Node Connection Visualization
 *
 *    +-------+
 *    | RESET |
 *    +-------+
 *        |
 *        | ibv_modify_qp() → INIT
 *        v
 *    +------+
 *    | INIT |
 *    +------+
 *        |
 *        | ibv_modify_qp() → RTR (needs remote info: LID/GID, QPN, PSN, pkey_index)
 *        v
 *    +-----+
 *    | RTR |  Ready to Receive
 *    +-----+
 *        |
 *        | ibv_modify_qp() → RTS (now can transmit)
 *        v
 *    +-----+
 *    | RTS |  Ready to Send
 *    +-----+
 *        |
 *        +--> (error / drain states if QP reset/terminated)
 *
 *
 * Visualizing Node A <--> Node B Connection
 *
 * Node A                               Node B
 * -------                              -------
 * QP (INIT)                            QP (INIT)
 *   |                                      |
 *   | exchange {QPN, PSN, LID, GID, ...}  |
 *   |--------------------------TCP-------->|
 *   |                                      |
 * QP (RTR)   <--------------------------> QP (RTR)
 *   |                                      |
 *   |          Both sides ready            |
 *   v                                      v
 * QP (RTS)   <--------------------------> QP (RTS)
 *   |                                      |
 *   |     Now both can send/recv packets   |
 */


#define SIZE 4096

struct conn_info {
    uint32_t qpn;
    uint32_t psn;
    uint32_t rkey;
    uint64_t vaddr;
    uint8_t gid[16]; // For RoCE addressing
};

// Exit on error with perror
static void die(const char *s) {
    perror(s);
    exit(1);
}

// Get local GID for RoCE v2
static void get_local_gid(struct ibv_context *ctx, int port, int index, union ibv_gid *gid) {
    if (ibv_query_gid(ctx, port, index, gid))
        die("ibv_query_gid");
}

// TCP connect to server
int tcp_connect_to(const char *server_ip, const char *port) {
    struct addrinfo hints = {0}, *res;
    int sock = -1;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(server_ip, port, &hints, &res) != 0)
	    die("getaddrinfo");

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) die("socket client");

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0)
	    die("connect client");

    freeaddrinfo(res);
    return sock;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    const char *port_str = argv[2];

    /* ---------------------------------------------------------
     * 1. Get the list of InfiniBand devices in the system
     *    - Use ibv_get_device_list() to enumerate available HCAs
     *      (Host Channel Adapters)
     *    - Typically, select the first device or based on some
     *      criteria like device name or port count
     * --------------------------------------------------------- */
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list)
	    die("ibv_get_device_list");


    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    if (!ctx)
	    die("ibv_open_device");
    /* ---------------------------------------------------------
     * 2. Allocate a Protection Domain (PD)
     *    - A PD is like a container that groups together
     *      memory regions, queue pairs, and other RDMA resources.
     * --------------------------------------------------------- */

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd)
	    die("ibv_alloc_pd");

    // Allocate a buffer for sending data
    char *buf = malloc(SIZE);
    if (!buf)
	    die("malloc");

    memset(buf, 0, SIZE);
    strcpy(buf, "Sid: Client writes this via IB_WR_RDMA_WRITE");

    /* ---------------------------------------------------------
     * 3. Register a Memory Region (MR)
     *    - RDMA requires memory to be "pinned" (page-locked) and
     *      registered with the NIC.
     *    - Access flags define what remote peers are allowed to
     *      do with this memory (e.g., local write, remote read/write).
     * --------------------------------------------------------- */
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, SIZE, IBV_ACCESS_LOCAL_WRITE);
    if (!mr)
	    die("ibv_reg_mr");

    /* ---------------------------------------------------------
     * 4. Create a Completion Queue (CQ)
     *    - Used by the NIC to notify when Work Requests (WRs) finish
     *    - Capacity here = 10 entries
     * --------------------------------------------------------- */
    struct ibv_cq *cq = ibv_create_cq(ctx, 10, NULL, NULL, 0);
    if (!cq)
	    die("ibv_create_cq");

    /* ---------------------------------------------------------
     * 5. Create a Queue Pair (QP)
     *    - QP = Send Queue + Receive Queue
     *    - QPs are like "sockets" in the InfiniBand world
     * --------------------------------------------------------- */
    struct ibv_qp_init_attr qp_init_attr = {0};

    /* QP Creation Attributes (qp_init_attr):
     *
     * qp_init_attr.send_cq   = cq
     * qp_init_attr.recv_cq   = cq
     *   - Associate the QP with the Completion Queue (CQ) for send
     *     and receive completions
     *
     * qp_init_attr.qp_type   = IBV_QPT_RC
     *   - QP type: Reliable Connection (RC), similar to TCP semantics
     *
     * qp_init_attr.cap.max_send_wr = 10
     *   - Maximum number of outstanding send Work Requests (WRs)
     *
     * qp_init_attr.cap.max_recv_wr = 10
     *   - Maximum number of outstanding receive Work Requests (WRs)
     *
     * qp_init_attr.cap.max_send_sge = 1
     * qp_init_attr.cap.max_recv_sge = 1
     *   - Maximum scatter/gather entries per WR for send and receive
     */
    qp_init_attr.send_cq = cq;		// Associate with our CQ
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;	// Reliable Connection (like TCP)
    qp_init_attr.cap.max_send_wr = 10;	// Max outstanding send WRs
    qp_init_attr.cap.max_recv_wr = 10;	// Max outstanding recv WRs
    qp_init_attr.cap.max_send_sge = 1;	// Max scatter/gather entries per WR
    qp_init_attr.cap.max_recv_sge = 1;

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp)
	    die("ibv_create_qp");

    // Move QP to INIT
    struct ibv_qp_attr attr = {0};

    /* QP INIT (Ready to Initialize) attributes:
     *
     * attr.qp_state       = IBV_QPS_INIT
     *   - Sets the QP state to INIT
     *
     * attr.pkey_index     = 0
     *   - Index into the HCA port's PKey table
     *   - Determines the partition the QP belongs to
     *
     * attr.port_num       = 1
     *   - Local HCA port used for communication
     *
     * attr.qp_access_flags = 0
     *   - Access permissions for this QP
     *   - Here, 0 means the client does not expose its
     *     memory region for remote RDMA operations
     */
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = 1;
    attr.qp_access_flags = 0; // client does not expose MR for remote ops
    if (ibv_modify_qp(qp, &attr,
                      IBV_QP_STATE |
                      IBV_QP_PKEY_INDEX |
                      IBV_QP_PORT |
                      IBV_QP_ACCESS_FLAGS))
	    die("ibv_modify_qp to INIT");

    /*
     * At this point:
     * - You must exchange QP information with the remote peer:
     *     (QP number, LID, PSN, rkey, remote_addr)
     * - Then move the QP through states: INIT → RTR → RTS 
     *   using ibv_modify_qp().
     * - This exchange usually requires an out-of-band channel 
     *   (for example, a TCP socket).
     */

    // Prepare local connection info
    struct conn_info local = {0};
    local.qpn = qp->qp_num;
    local.psn = (uint32_t)(rand() & 0xffffff);
    local.rkey = mr->rkey; // local MR rkey (for demonstration)
    local.vaddr = (uintptr_t)buf;

    // Local GID
    union ibv_gid gid;
    get_local_gid(ctx, 1, 0, &gid);
    memcpy(local.gid, gid.raw, 16);

    // TCP connect and exchange conn_info
    int sock = tcp_connect_to(server_ip, port_str);
    struct conn_info remote;
    if (read(sock, &remote, sizeof(remote)) != sizeof(remote))
	    die("read remote info");
    if (write(sock, &local, sizeof(local)) != sizeof(local))
	    die("write local info");

    // Print exchanged info
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

    /*
     * attr.max_dest_rd_atomic = 1;
     *
     * Maximum number of outstanding RDMA read/atomic operations that this QP can accept from the remote peer.
     * It defines how many “in-flight” RDMA read/atomic requests the remote can issue before waiting for completions.
     * Typical values:
     * 1 → allow one outstanding RDMA read/atomic.
     * Higher values increase concurrency but consume more resources on the responder side.
     * This is a capability of the target QP (the responder), not the initiator.
     *
     * 
     * attr.max_dest_rd_atomic = 1;
     *
     * Maximum number of outstanding RDMA read/atomic operations 
     * that this QP can accept from the remote peer.
     *
     * - Defines how many “in-flight” RDMA read/atomic requests 
     *   the remote can issue before waiting for completions.
     * - Typical values:
     *     1 → allow one outstanding RDMA read/atomic.
     *     Higher values → increase concurrency but consume more
     *     responder-side resources.
     * - This is a capability of the target QP (the responder),
     *   not the initiator.
     *
     *
     * attr.min_rnr_timer = 12;
     *
     * Minimum RNR NAK (Receiver Not Ready) timer value.
     *
     * - If a QP’s receive queue is empty and it receives a packet
     *   that requires a WR, it replies with an RNR NAK.
     * - This timer controls how long the sender must wait before 
     *   retrying after receiving an RNR NAK.
     * - The value is encoded (not raw microseconds). According to
     *   the IBTA spec:
     *
     *     Time = 2^(min_rnr_timer) * 655.36 µs
     *
     * - For min_rnr_timer = 12:
     *     Time = 2^12 * 655.36 µs ≈ 2.68 seconds.
     *
     * - Bigger values → more tolerance for slow receivers to post recvs.
     * - Smaller values → faster retries but risk NAK storms.
     *
     *
     * attr.ah_attr.is_global = 1;
     * 1 if RoCE/IPv6 GID used, Tells the HCA that the destination address
     * is global (needs a GRH – Global Routing Header).
     * 
     * memcpy(&attr.ah_attr.grh.dgid, remote.gid, 16);
     *
     * Sets the destination GID (Global Identifier) of the remote peer.
     * remote’s 128-bit IPv6-like address (in RoCE this 
     * encodes IP/MAC-based identity).
     * 
     * attr.ah_attr.grh.sgid_index = 0;
     *
     * Meaning: Selects which local GID (from the port’s GID table) to use as the source GID.
     * Each port may expose multiple GIDs (e.g. one per VLAN, per RoCE v1/v2, or per IP subnet).
     * Here 0 means: use the first GID entry of that port.
     *
     * attr.ah_attr.grh.hop_limit = 1;
     *
     * Meaning: Sets the IPv6 hop limit in the GRH (similar to TTL in IP).
     * 1 means the packet is only valid for direct neighbor (no routing).
     * For multi-hop routed networks, you’d set this higher (like 64).
     */
    // Move QP to RTR
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
                      IBV_QP_MAX_DEST_RD_ATOMIC))
	    die("ibv_modify_qp to RTR");

    // Move QP to RTS
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    /* QP RTS (Ready to Send) attributes:
     *
     * attr.timeout      = 14
     *   - Local ACK timeout for sent packets (in IB TA units)
     *
     * attr.retry_cnt    = 7
     *   - Number of times the sender retries after failed sends
     *
     * attr.rnr_retry    = 7
     *   - Number of times to retry when remote returns RNR NAK
     *     (7 = infinite retry)
     *
     * attr.sq_psn       = local.psn
     *   - Starting Packet Sequence Number for this QP's Send Queue
     *
     * attr.max_rd_atomic = 1
     *   - Maximum number of outstanding RDMA read/atomic operations
     *     that the local QP can issue to the remote peer
     */
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
                      IBV_QP_MAX_QP_RD_ATOMIC))
	    die("ibv_modify_qp to RTS");

    printf("QP moved to RTS. Posting RDMA_WRITE...\n");

    /* ---------------------------------------------------------
     * 6. Post RDMA_WRITE
     *
     * Describe our buffer in a Scatter/Gather Entry (SGE)
     *   - The NIC needs to know which local buffer(s) we want to send
     *   - SGE contains pointer to buffer, length, and local key (lkey)
     * --------------------------------------------------------- */
    struct ibv_sge sge;

    sge.addr = (uintptr_t)buf;
    sge.length = SIZE;
    sge.lkey = mr->lkey;

    /* ---------------------------------------------------------
     * 7. Prepare a Work Request (WR) for RDMA Write
     *
     * Tells the NIC: "Write my buffer into remote memory"
     *   - Includes scatter/gather list, remote address, and rkey
     *   - Set wr_id and send_flags (e.g., IBV_SEND_SIGNALED)
     * --------------------------------------------------------- */
    struct ibv_send_wr wr = {0}, *bad_wr;

    /* RDMA Work Request (WR) setup for sending:
     *
     * wr.wr_id        = 1
     *   - Unique identifier for this WR; used in completion notifications
     *
     * wr.sg_list      = &sge
     *   - Pointer to the scatter-gather element(s) describing local buffer(s)
     *
     * wr.num_sge      = 1
     *   - Number of entries in the scatter-gather list (here, 1)
     *
     * wr.opcode       = IBV_WR_RDMA_WRITE
     *   - Operation type: here, an RDMA write to remote memory
     *
     * wr.send_flags   = IBV_SEND_SIGNALED
     *   - Request a completion notification when this WR is finished
     *
     * Remote information (obtained from peer via out-of-band channel):
     * wr.wr.rdma.remote_addr = remote.vaddr
     *   - Target address in remote memory where data will be written
     *
     * wr.wr.rdma.rkey       = remote.rkey
     *   - Remote key associated with the remote memory region
     *     (grants access permissions for the RDMA operation)
     */
    wr.wr_id = 1;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remote.vaddr;
    wr.wr.rdma.rkey = remote.rkey;

    /* ---------------------------------------------------------
     * 8. Post the Work Request into the Send Queue
     *
     * Queues the RDMA operation into the NIC hardware
     *   - The NIC will process the WR asynchronously
     *   - Completion will be reported via the associated Completion Queue (CQ)
     * --------------------------------------------------------- */
    if (ibv_post_send(qp, &wr, &bad_wr))
	    die("ibv_post_send");

    /* ---------------------------------------------------------
     * 9. Poll the Completion Queue (CQ) for the result
     *
     * Block (or poll) until the NIC reports that the Work Request (WR) is complete
     *   - Use ibv_poll_cq() to check for completions
     *   - Completion entry contains wr_id and status
     * --------------------------------------------------------- */
    struct ibv_wc wc;

    int ne;
    do { ne = ibv_poll_cq(cq, 1, &wc); } while (ne == 0);
    if (ne < 0)
	    die("ibv_poll_cq");

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "RDMA_WRITE failed: wc.status=%d\n", wc.status);
        exit(1);
    }

    printf("RDMA_WRITE completed on client side (CQ).\n");

    // Notify server
    const char *done = "DONE";
    if (write(sock, done, strlen(done)+1) < 0)
	    die("notify server");

    // Cleanup
    close(sock);
    ibv_dereg_mr(mr);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    free(buf);

    return 0;
}

