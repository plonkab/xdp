#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE

#include <sys/resource.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include <xdp/xsk.h>
#include <linux/if_link.h>

#define INTERFACE_INDEX 2
#define INTERFACE_NAME "lo"
#define INTERFACE_QUEUE_INDEX 0
#define NUM_FRAMES 4096
#define XSK_FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define ETH_FRAME_SIZE 1000
#define XDP_FLAGS XDP_FLAGS_SKB_MODE
#define XDP_BIND_FLAGS XDP_COPY
#define INVALID_UMEM_FRAME UINT64_MAX
#define PERIOD_NS 1000000
#define NS_PER_S 1000000000
#define BATCH_SIZE 1

typedef __u32 u32;

struct xsk_umem_info
{
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem *umem;
    void *buffer;
};

struct xsk_socket_info
{
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_umem_info *umem;
    struct xsk_socket *xsk;

    uint64_t umem_frame_addr[NUM_FRAMES];
    uint32_t umem_frame_free;

    uint32_t outstanding_tx;
};

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size);
static struct xsk_socket_info *xsk_configure_socket(struct xsk_umem_info *umem);
static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk);
static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame);
static struct timespec create_timespec(const uint64_t time);
static void create_frame(uint8_t *const frame);
static void complete_tx(struct xsk_socket_info *xsk);

int main(int argc, char *argv[])
{
    void *packet_buffer;

    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    if (setrlimit(RLIMIT_MEMLOCK, &rlim))
    {
        fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK) \"%s\"\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    uint64_t packet_buffer_size = NUM_FRAMES * XSK_FRAME_SIZE;
    if (posix_memalign(&packet_buffer,
                       getpagesize(), /* PAGE_SIZE aligned */
                       packet_buffer_size))
    {
        fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct xsk_umem_info *const umem = configure_xsk_umem(packet_buffer, packet_buffer_size);
    if (umem == NULL)
    {
        fprintf(stderr, "ERROR: Can't create umem \"%s\"\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct xsk_socket_info *const xsk_socket = xsk_configure_socket(umem);
    if (xsk_socket == NULL)
    {
        fprintf(stderr, "ERROR: Can't setup AF_XDP socket \"%s\"\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    while (true)
    {
        const struct timespec ts = create_timespec(PERIOD_NS);
        if (clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL) != 0)
        {
            perror("Sleep failed");
            exit(EXIT_FAILURE);
        }

        u32 idx;
        while (xsk_ring_prod__reserve(&xsk_socket->tx, BATCH_SIZE, &idx) < BATCH_SIZE)
        {
            complete_tx(xsk_socket);
        }

        for (unsigned int i = 0; i < BATCH_SIZE; i++)
        {
            struct xdp_desc *const tx_desc = xsk_ring_prod__tx_desc(&xsk_socket->tx, idx + i);
            const uint64_t addr = xsk_alloc_umem_frame(xsk_socket);
            if (addr == INVALID_UMEM_FRAME)
            {
                fprintf(stderr, "Failed to allocate UMEM frame\n");
                exit(EXIT_FAILURE);
            }
            uint8_t *const pkt = xsk_umem__get_data(xsk_socket->umem->buffer, addr);
            create_frame(pkt);
            tx_desc->addr = addr;
            tx_desc->len = ETH_FRAME_SIZE;
        }

        xsk_ring_prod__submit(&xsk_socket->tx, BATCH_SIZE);
        xsk_socket->outstanding_tx += BATCH_SIZE;
        complete_tx(xsk_socket);
    }
}

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
{
    struct xsk_umem_info *umem;
    int ret;

    umem = calloc(1, sizeof(*umem));
    if (!umem)
        return NULL;

    ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq,
                           NULL);
    if (ret)
    {
        errno = -ret;
        return NULL;
    }

    umem->buffer = buffer;
    return umem;
}

static struct xsk_socket_info *xsk_configure_socket(struct xsk_umem_info *umem)
{
    struct xsk_socket_config xsk_cfg;
    struct xsk_socket_info *xsk_info;
    int i;
    int ret;
    uint32_t prog_id;

    xsk_info = calloc(1, sizeof(*xsk_info));
    if (!xsk_info)
        return NULL;

    xsk_info->umem = umem;
    xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    xsk_cfg.xdp_flags = XDP_FLAGS;
    xsk_cfg.bind_flags = XDP_BIND_FLAGS;
    xsk_cfg.libbpf_flags = 0;
    ret = xsk_socket__create(&xsk_info->xsk, INTERFACE_NAME,
                             INTERFACE_QUEUE_INDEX, umem->umem, &xsk_info->rx,
                             &xsk_info->tx, &xsk_cfg);
    if (ret)
        goto error_exit;

    /* Getting the program ID must be after the xdp_socket__create() call */
    if (bpf_xdp_query_id(INTERFACE_INDEX, XDP_FLAGS, &prog_id))
        goto error_exit;

    for (i = 0; i < NUM_FRAMES; i++)
        xsk_info->umem_frame_addr[i] = i * XSK_FRAME_SIZE;
    xsk_info->umem_frame_free = NUM_FRAMES;

    return xsk_info;

error_exit:
    errno = -ret;
    return NULL;
}

static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
    uint64_t frame;
    if (xsk->umem_frame_free == 0)
        return INVALID_UMEM_FRAME;

    frame = xsk->umem_frame_addr[--xsk->umem_frame_free];
    xsk->umem_frame_addr[xsk->umem_frame_free] = INVALID_UMEM_FRAME;
    return frame;
}

static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame)
{
    assert(xsk->umem_frame_free < NUM_FRAMES);

    xsk->umem_frame_addr[xsk->umem_frame_free++] = frame;
}

static struct timespec create_timespec(const uint64_t time)
{
    const struct timespec ts = {.tv_sec = time / NS_PER_S, .tv_nsec = time % NS_PER_S};
    return ts;
}

static void create_frame(uint8_t *const frame)
{
    memset(frame, 0, ETH_FRAME_SIZE);
    frame[0] = 0x11;
    frame[1] = 0x22;
    frame[2] = 0x33;
    frame[3] = 0x44;
    frame[4] = 0x55;
    frame[5] = 0x66;
    frame[6] = 0x77;
    frame[7] = 0x88;
}

static void complete_tx(struct xsk_socket_info *xsk)
{
    unsigned int completed;
    uint32_t idx_cq;

    if (!xsk->outstanding_tx)
        return;

    sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

    /* Collect/free completed TX buffers */
    completed = xsk_ring_cons__peek(&xsk->umem->cq, XSK_RING_CONS__DEFAULT_NUM_DESCS, &idx_cq);

    if (completed > 0)
    {
        for (int i = 0; i < completed; i++)
            xsk_free_umem_frame(xsk, *xsk_ring_cons__comp_addr(&xsk->umem->cq, idx_cq + i));

        xsk_ring_cons__release(&xsk->umem->cq, completed);
        xsk->outstanding_tx -= completed < xsk->outstanding_tx ? completed : xsk->outstanding_tx;
    }
}