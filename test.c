#include "xdp/libxdp.h"

int main(int argc, char *argv[])
{
    struct xdp_program *prog;

    prog = xdp_program__open_file("prog.o", "xdp", NULL);

    return 0;
}