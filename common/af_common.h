#pragma once

#define NUM_FRAMES 4096
#define INVALID_UMEM_FRAME UINT64_MAX

#include <stdint.h>
#include <xdp/xsk.h>

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

struct xsk_socket_info *create_socket(const char *const interface_name, const unsigned int queue_num,
                                      const uint32_t xdp_flags, const uint16_t bind_flags);
uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk);
void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame);