#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <poll.h>

#include <linux/if_link.h>

#include "common/af_common.h"

#define INTERFACE_QUEUE_INDEX 0
#define ETH_FRAME_SIZE 1000
#define XDP_FLAGS XDP_FLAGS_SKB_MODE
#define XDP_BIND_FLAGS XDP_COPY
#define RX_BATCH_SIZE 64

static bool _init_fill_queue(struct xsk_socket_info *const xsk_socket);
static void _handle_receive_packets(struct xsk_socket_info *const xsk_socket);
static void _process_packet(const uint8_t* const pkt, const uint32_t len);
static void _fill_fq(struct xsk_socket_info *const xsk_socket);

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Provide device name\n");
        exit(EXIT_FAILURE);
    }

    struct xsk_socket_info *const xsk_socket = create_socket(argv[1], INTERFACE_QUEUE_INDEX, XDP_FLAGS, XDP_BIND_FLAGS);
    if (!xsk_socket)
    {
        exit(EXIT_FAILURE);
    }

    if (!_init_fill_queue(xsk_socket))
    {
        exit(EXIT_FAILURE);
    }

    struct pollfd fds[2];
    int ret, nfds = 1;

    memset(fds, 0, sizeof(fds));
    fds[0].fd = xsk_socket__fd(xsk_socket->xsk);
    fds[0].events = POLLIN;

    while (true)
    {
        ret = poll(fds, nfds, -1);
        if (ret <= 0 || ret > 1)
            continue;
        _handle_receive_packets(xsk_socket);
    }
}

static bool _init_fill_queue(struct xsk_socket_info *const xsk_socket)
{
    uint32_t idx;
    const int ret = xsk_ring_prod__reserve(&xsk_socket->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);

    if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
    {
        perror("Failed to allocate fill queue descriptors");
        return false;
    }

    for (int i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
        *xsk_ring_prod__fill_addr(&xsk_socket->umem->fq, idx++) = xsk_alloc_umem_frame(xsk_socket);

    xsk_ring_prod__submit(&xsk_socket->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return true;
}

static void _handle_receive_packets(struct xsk_socket_info *const xsk_socket)
{
    uint32_t idx_rx = 0;

    const unsigned int rcvd = xsk_ring_cons__peek(&xsk_socket->rx, RX_BATCH_SIZE, &idx_rx);
    if (!rcvd)
        return;

    for (unsigned int i = 0; i < rcvd; i++)
    {
        const struct xdp_desc *const xdp_desc = xsk_ring_cons__rx_desc(&xsk_socket->rx, idx_rx++);
        const uint64_t addr = xdp_desc->addr;
        const uint32_t len = xdp_desc->len;

        _process_packet(xsk_umem__get_data(xsk_socket->umem->buffer, addr), len);
        xsk_free_umem_frame(xsk_socket, addr);
    }

    xsk_ring_cons__release(&xsk_socket->rx, rcvd);

    _fill_fq(xsk_socket);
}

static void _process_packet(const uint8_t* const pkt, const uint32_t len)
{
    const uint16_t eth_type = (pkt[12] << 8) + pkt[13];
    printf("eth type %x\n", eth_type);
}

static void _fill_fq(struct xsk_socket_info *const xsk_socket)
{
    const unsigned int stock_frames = xsk_prod_nb_free(&xsk_socket->umem->fq, xsk_umem_free_frames(xsk_socket));

    if (stock_frames > 0)
    {
        uint32_t idx_fq = 0;
        unsigned int ret = xsk_ring_prod__reserve(&xsk_socket->umem->fq, stock_frames, &idx_fq);

        while (ret != stock_frames)
            ret = xsk_ring_prod__reserve(&xsk_socket->umem->fq, stock_frames, &idx_fq);

        for (unsigned int i = 0; i < stock_frames; i++)
            *xsk_ring_prod__fill_addr(&xsk_socket->umem->fq, idx_fq++) = xsk_alloc_umem_frame(xsk_socket);

        xsk_ring_prod__submit(&xsk_socket->umem->fq, stock_frames);
    }
}