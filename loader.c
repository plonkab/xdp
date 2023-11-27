#include <stdio.h>
#include "xdp/libxdp.h"

int main(int argc, char *argv[])
{
#define IFINDEX 1

    struct xdp_program *prog;
    int err;

    prog = xdp_program__open_file("prog.o", "xdp", NULL);
    err = xdp_program__attach(prog, IFINDEX, XDP_MODE_UNSPEC, 0);

    if (!err)
        xdp_program__detach(prog, IFINDEX, XDP_MODE_UNSPEC, 0);

    xdp_program__close(prog);
}