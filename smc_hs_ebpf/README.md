## Commands to run ebpf programm

bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
clang -g -O2 -target bpf -D__TARGET_ARCH_s390 -c smc_hooks_minimal.bpf.c -o smc_hooks_minimal.bpf.o
bpftool gen skeleton smc_hooks_minimal.bpf.o name smc_hooks_minimal > smc_hooks_minimal.skel.h
cc -O2 smc_hooks_minimal.c -o smc_hooks_minimal $(pkg-config --cflags --libs libbpf)
