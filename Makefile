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

all: lib simple_xdp af_xdp 

simple_xdp: simple_xdp_user simple_xdp_kern;

af_xdp: af_xdp_kern af_xdp_user;

lib: libbpf libxdp;

libxdp: $(LIB_XDP_OBJ)

libbpf: $(LIB_BPF_OBJ)

$(LIB_BPF_OBJ): 
	$(Q)$(MAKE) -C $(LIB_BPF_DIR) DESTDIR=$(LIB_INSTALL) BUILD_STATIC_ONLY=y PREFIX=  LIBDIR=lib  install

$(LIB_XDP_OBJ): libbpf
	$(Q)$(MAKE) -C $(LIB_XDP_DIR) DESTDIR=$(LIB_INSTALL) BUILD_STATIC_ONLY=y PREFIX=  FORCE_SUBDIR_LIBBPF=1 libxdp_install

simple_xdp_user: % : %.c lib
	$(Q)$(CC) $(CC_FLAGS) -I $(LIB_INSTALL_INCLUDE) -L $(LIB_INSTALL_LIB) -o $@ $< -l:libxdp.a -l:libbpf.a -lelf -lz

simple_xdp_kern: % : %.o;

simple_xdp_kern.o: %.o : %.c
	$(CLANG) -g -Wall -target bpf -c $< -o $@

COMMON_OBJECTS = common/common_params.o common/common_user_bpf_xdp.o

$(COMMON_OBJECTS): %.o : %.c %.h
	$(Q)$(MAKE) -C common LIB_INSTALL_INCLUDE=$(LIB_INSTALL_INCLUDE)

af_xdp_user: %:%.c $(COMMON_OBJECTS)
	$(Q)$(CC) $(CC_FLAGS) -I $(LIB_INSTALL_INCLUDE) -L $(LIB_INSTALL_LIB) -o $@ $? -l:libxdp.a -l:libbpf.a -lelf -lz

af_xdp_kern: % : %.o;

af_xdp_kern.o: %.o : %.c
	$(Q)$(CLANG)  -g -Wall -target bpf -c $< -o $@

clean: clean_simple_xdp clean_af_xdp
	$(Q)rm -f $(LIB_XDP_OBJ)
	$(Q)rm -f $(LIB_BPF_OBJ)
	$(Q)$(MAKE) -C $(LIB_XDP_DIR) clean
	$(Q)$(MAKE) -C common clean
	$(Q)rm -fr $(LIB_INSTALL)/*

clean_simple_xdp:
	$(Q)rm -f simple_xdp_user
	$(Q)rm -f simple_xdp_kern.o

clean_af_xdp:
	$(Q)rm -f af_xdp_user
	$(Q)rm -f af_xdp_kern.o