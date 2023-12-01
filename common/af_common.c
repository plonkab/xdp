#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE

#include "af_common.h"

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include <net/if.h>

#define XSK_FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size);
static struct xsk_socket_info *xsk_configure_socket(const char *const interface_name, const unsigned int queue_num,
                                             const uint32_t xdp_flags, const uint16_t bind_flags,
                                             struct xsk_umem_info *umem);

struct xsk_socket_info *create_socket(const char *const interface_name, const unsigned int queue_num,
                                      const uint32_t xdp_flags, const uint16_t bind_flags)
{
    void *packet_buffer;
   
    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    if (setrlimit(RLIMIT_MEMLOCK, &rlim))
    {
        fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK) \"%s\"\n",
                strerror(errno));
        return NULL;
    }

    uint64_t packet_buffer_size = NUM_FRAMES * XSK_FRAME_SIZE;
    if (posix_memalign(&packet_buffer,
                       getpagesize(), /* PAGE_SIZE aligned */
                       packet_buffer_size))
    {
        fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n",
                strerror(errno));
        return NULL;
    }

    struct xsk_umem_info *const umem = configure_xsk_umem(packet_buffer, packet_buffer_size);
    if (umem == NULL)
    {
        fprintf(stderr, "ERROR: Can't create umem \"%s\"\n",
                strerror(errno));
        return NULL;
    }

    struct xsk_socket_info *const xsk_socket = xsk_configure_socket(interface_name, queue_num, xdp_flags, bind_flags, umem);
    if (xsk_socket == NULL)
    {
        fprintf(stderr, "ERROR: Can't setup AF_XDP socket \"%s\"\n",
                strerror(errno));
        return NULL;
    }

    return xsk_socket;
}

uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
    uint64_t frame;
    if (xsk->umem_frame_free == 0)
        return INVALID_UMEM_FRAME;

    frame = xsk->umem_frame_addr[--xsk->umem_frame_free];
    xsk->umem_frame_addr[xsk->umem_frame_free] = INVALID_UMEM_FRAME;
    return frame;
}

void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame)
{
    assert(xsk->umem_frame_free < NUM_FRAMES);

    xsk->umem_frame_addr[xsk->umem_frame_free++] = frame;
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

static struct xsk_socket_info *xsk_configure_socket(const char *const interface_name, const unsigned int queue_num,
                                             const uint32_t xdp_flags, const uint16_t bind_flags,
                                             struct xsk_umem_info *umem)
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
    xsk_cfg.xdp_flags = xdp_flags;
    xsk_cfg.bind_flags = bind_flags;
    xsk_cfg.libbpf_flags = 0;
    ret = xsk_socket__create(&xsk_info->xsk, interface_name,
                             queue_num, umem->umem, &xsk_info->rx,
                             &xsk_info->tx, &xsk_cfg);
    if (ret)
        goto error_exit;

    const unsigned int interface_index = if_nametoindex(interface_name);
    if (interface_index == 0)
    {
        perror("Failed to get device index");
        return NULL;
    }

    /* Getting the program ID must be after the xdp_socket__create() call */
    if (bpf_xdp_query_id(interface_index, xdp_flags, &prog_id))
        goto error_exit;

    for (i = 0; i < NUM_FRAMES; i++)
        xsk_info->umem_frame_addr[i] = i * XSK_FRAME_SIZE;
    xsk_info->umem_frame_free = NUM_FRAMES;

    return xsk_info;

error_exit:
    errno = -ret;
    return NULL;
}