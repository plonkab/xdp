#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include <linux/if_link.h>

#include "common/af_common.h"

#define INTERFACE_QUEUE_INDEX 0
#define ETH_FRAME_SIZE 1000
#define XDP_FLAGS XDP_FLAGS_SKB_MODE
#define XDP_BIND_FLAGS XDP_COPY
#define PERIOD_NS 1000000
#define NS_PER_S 1000000000
#define BATCH_SIZE 1

static struct timespec create_timespec(const uint64_t time);
static void create_frame(uint8_t *const frame);
static void complete_tx(struct xsk_socket_info *xsk);

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

    while (true)
    {
        const struct timespec ts = create_timespec(PERIOD_NS);
        if (clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL) != 0)
        {
            perror("Sleep failed");
            exit(EXIT_FAILURE);
        }

        uint32_t idx;
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