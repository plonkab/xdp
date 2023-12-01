all: common_params.o common_user_bpf_xdp.o af_common.o

common_params.o: common_params.c common_params.h
	$(Q)$(CC) $(CC_FLAGS) -I $(LIB_INSTALL_INCLUDE)  -c -o $@ $<

common_user_bpf_xdp.o: common_user_bpf_xdp.c common_user_bpf_xdp.h
	$(Q)$(CC) $(CC_FLAGS) -I $(LIB_INSTALL_INCLUDE)  -c -o $@ $<

af_common.o: %.o : %.c %.h
	$(Q)$(CC) $(CC_FLAGS) -I $(LIB_INSTALL_INCLUDE)  -c -o $@ $<

.PHONY: clean

clean:
	$(Q)rm -f *.o
