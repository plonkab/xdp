#include <stdio.h>
#include "xdp/libxdp.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Provide program file\n");
        return 1;
    }

#define IFINDEX 1

    struct xdp_program *prog;
    int err;

    prog = xdp_program__open_file(argv[1], argv[2], NULL);
    err = xdp_program__attach(prog, IFINDEX, XDP_MODE_UNSPEC, 0);

    if (!err)
        xdp_program__detach(prog, IFINDEX, XDP_MODE_UNSPEC, 0);

    xdp_program__close(prog);
}