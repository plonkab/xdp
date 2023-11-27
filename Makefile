CC = gcc
CLANG = clang
LLC = llc

Q = @

ROOT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

CC_FLAGS = -g -Werror -Wall -fPIC

LIB_DIR := $(ROOT_DIR)/lib/
LIB_INSTALL := $(LIB_DIR)/install/
LIB_INSTALL_LIB := $(LIB_INSTALL)/lib/
LIB_INSTALL_INCLUDE := $(LIB_INSTALL)/include/

LIB_XDP_OBJ := $(LIB_INSTALL_LIB)/libxdp.a
LIB_XDP_DIR := $(LIB_DIR)/xdp-tools/

LIB_BPF_OBJ := $(LIB_INSTALL_LIB)/libbpf.a
LIB_BPF_DIR := $(LIB_XDP_DIR)/lib/libbpf/src/

export LIBBPF_DIR := $(LIB_BPF_DIR)/..
export LIBBPF_INCLUDE_DIR := $(LIB_INSTALL_INCLUDE)
export LIBBPF_UNBUILT := 1

all: libxdp libbpf loader
libxdp: $(LIB_XDP_OBJ)
libbpf: $(LIB_BPF_OBJ)

$(LIB_BPF_OBJ): 
	$(Q)$(MAKE) -C $(LIB_BPF_DIR) DESTDIR=$(LIB_INSTALL) BUILD_STATIC_ONLY=y PREFIX=  LIBDIR=lib  install

$(LIB_XDP_OBJ): libbpf
	$(Q)$(MAKE) -C $(LIB_XDP_DIR) DESTDIR=$(LIB_INSTALL) BUILD_STATIC_ONLY=y PREFIX=  FORCE_SUBDIR_LIBBPF=1 libxdp_install

loader: % : %.c libxdp libbpf prog
	$(Q)$(CC) $(CC_FLAGS) -I $(LIB_INSTALL_INCLUDE) -L $(LIB_INSTALL_LIB) -o $@ $< -l:libxdp.a -l:libbpf.a -lelf -lz

prog: % : %.c
	$(Q)$(CLANG) -O2 -g -Wall -target bpf -c $< -o $(@:=.o)

clean: clean_loader clean_prog
	$(Q)rm -f $(LIB_XDP_OBJ)
	$(Q)rm -f $(LIB_BPF_OBJ)
	$(Q)$(MAKE) -C $(LIB_XDP_DIR) clean
	$(Q)rm -fr $(LIB_INSTALL)/*

clean_loader:
	$(Q)rm -f loader

clean_prog:
	$(Q)rm -f prog.o