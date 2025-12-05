/*
 * kvm.c - Minimal AArch64 userspace KVM example for Raspberry Pi 5
 *
 * Purpose:
 *   - Create a VM and a vCPU via /dev/kvm ioctls
 *   - Map guest memory into userspace and register via KVM_SET_USER_MEMORY_REGION
 *   - Load a tiny flat binary guest (guest.bin) at a guest physical address
 *   - Set the guest PC to the guest entry address using KVM_SET_ONE_REG
 *   - Run the vCPU with KVM_RUN and handle MMIO and HLT exits
 *
 * Notes:
 *   - Designed to run on an aarch64 host with KVM (e.g., Raspberry Pi 5)
 *   - Run as root or a user in 'kvm' group with rw access to /dev/kvm
 *   - Build: gcc -O2 -Wall -o kvm kvm.c
 *
 * Limitations:
 *   - Minimal error handling for clarity (but prints perror on failures)
 *   - Assumes guest.bin is a flat binary linked at GUEST_LOAD_ADDR
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <linux/kvm.h>
#include <errno.h>

/* Configuration: change these if you want different layout */
#define GUEST_MEM_SIZE     0x00100000UL   /* 1 MiB per region (adjustable) */
#define GUEST_LOAD_ADDR    0x40000000UL   /* Guest physical address where guest.bin will be placed */
#define MMIO_ADDR          0x10000000ULL  /* Unmapped addr: guest stores here -> KVM_EXIT_MMIO */

static void die(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

/*
 * new_region() - create and register one userspace memory region for KVM
 *
 * vmfd:   file descriptor returned by KVM_CREATE_VM
 * slot:   region slot index (used to identify region in KVM)
 * guest_phys: guest physical base address for this region
 * mem_size:   size in bytes
 * readonly:   if non-zero mark region KVM_MEM_READONLY
 * mapped_out: if non-NULL, store the returned userspace pointer
 */
static void new_region(int vmfd, int slot, uint64_t guest_phys,
		size_t mem_size, int readonly, void **mapped_out)
{
	void *user_mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (user_mem == MAP_FAILED)
		die("mmap user_mem");

	if (mapped_out)
		*mapped_out = user_mem;

	struct kvm_userspace_memory_region region;
	memset(&region, 0, sizeof(region));
	region.slot = slot;
	region.flags = readonly ? KVM_MEM_READONLY : 0;
	region.guest_phys_addr = guest_phys;
	region.memory_size = mem_size;
	region.userspace_addr = (uint64_t)user_mem;

	if (ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region) < 0)
		die("KVM_SET_USER_MEMORY_REGION");
}

/* Helper to set a single 64-bit register using KVM_SET_ONE_REG
 * We compose the reg id using the documented macros for ARM64 core regs.
 *
 * For PC, we compute the index from offsetof(struct kvm_regs, regs.pc)
 * divided by 4 (the KVM reg id uses 32-bit words indexing).
 */
static void set_pc(int vcpufd, uint64_t pc_val)
{
	uint64_t index = offsetof(struct kvm_regs, regs.pc) / sizeof(__u32);
	uint64_t id = KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | index;

	/* KVM expects a userspace pointer where it will copy the reg value from */
	uint64_t pc_copy = pc_val;
	struct kvm_one_reg reg;
	reg.id = id;
	reg.addr = (uint64_t)&pc_copy;

	if (ioctl(vcpufd, KVM_SET_ONE_REG, &reg) < 0)
		die("KVM_SET_ONE_REG (pc)");
}

int main(void)
{
	/* Open /dev/kvm */
	int kvmfd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if (kvmfd < 0)
		die("open /dev/kvm");

	/* Verify API version */
	int api = ioctl(kvmfd, KVM_GET_API_VERSION, 0);
	if (api != KVM_API_VERSION) {
		fprintf(stderr, "Unexpected KVM API version: %d (expected %d)\n", api, KVM_API_VERSION);
		exit(EXIT_FAILURE);
	}

	/* Create VM */
	int vmfd = ioctl(kvmfd, KVM_CREATE_VM, (unsigned long)0);
	if (vmfd < 0)
		die("KVM_CREATE_VM");

	/* Create and register guest memory regions.
	 * We create:
	 *  - region 0: RAM at guest phys 0x0 (stack/data)
	 *  - region 1: code region at GUEST_LOAD_ADDR where guest.bin is loaded
	 *
	 * Note: We intentionally do NOT map MMIO_ADDR so guest stores there cause KVM_EXIT_MMIO.
	 */
	void *mem_low = NULL, *mem_code = NULL;
	new_region(vmfd, 0, 0x0, GUEST_MEM_SIZE, 0, &mem_low);
	new_region(vmfd, 1, GUEST_LOAD_ADDR, GUEST_MEM_SIZE, 0, &mem_code);

	/* Read guest binary (flat binary) into mem_code */
	int imgfd = open("./guest/guest.bin", O_RDONLY);
	if (imgfd < 0)
		die("open guest.bin");
	struct stat st;
	if (fstat(imgfd, &st) < 0)
		die("fstat guest.bin");
	if ((size_t)st.st_size > GUEST_MEM_SIZE)
		die("guest.bin too large for region");
	ssize_t r = read(imgfd, mem_code, st.st_size);
	if (r < 0 || (size_t)r != (size_t)st.st_size)
		die("read guest.bin");
	close(imgfd);

	/* Create vCPU */
	int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
	if (vcpufd < 0)
		die("KVM_CREATE_VCPU");

	/* Initialize vCPU using preferred target for ARM */
	struct kvm_vcpu_init target;
	if (ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, &target) < 0)
		die("KVM_ARM_PREFERRED_TARGET");
	if (ioctl(vcpufd, KVM_ARM_VCPU_INIT, &target) < 0)
		die("KVM_ARM_VCPU_INIT");

	/* Set guest PC to the start of our code region. */
	set_pc(vcpufd, GUEST_LOAD_ADDR);

	/* mmap kvm_run shared struct */
	int mmap_size = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (mmap_size <= 0)
		die("KVM_GET_VCPU_MMAP_SIZE");
	struct kvm_run *run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
	if (run == MAP_FAILED)
		die("mmap kvm_run");

	/* Run loop */
	while (1) {
		int ret = ioctl(vcpufd, KVM_RUN, 0);
		if (ret < 0 && errno == EINTR)
			continue;
		if (ret < 0)
			die("KVM_RUN");

		switch (run->exit_reason) {
			case KVM_EXIT_MMIO:
				/* If guest wrote to MMIO_ADDR (which is unmapped), KVM reports MMIO exit.
				 * We print written bytes to host stdout.
				 */
				if (run->mmio.is_write) {
					uint32_t len = run->mmio.len;
					for (uint32_t i = 0; i < len; ++i)
						putchar(run->mmio.data[i]);
					fflush(stdout);
				} else {
					fprintf(stderr, "Guest read MMIO at 0x%llx len=%u\n",
							(unsigned long long)run->mmio.phys_addr, run->mmio.len);
				}
				break;

			case KVM_EXIT_HLT:
				fprintf(stderr, "Guest executed HLT. Exiting.\n");
				goto cleanup;

			case KVM_EXIT_SHUTDOWN:
				fprintf(stderr, "Guest requested shutdown. Exiting.\n");
				goto cleanup;

			case KVM_EXIT_EXCEPTION:
				fprintf(stderr, "Guest exception. Exiting.\n");
				goto cleanup;

			default:
				fprintf(stderr, "Unhandled KVM exit reason: %d\n", run->exit_reason);
				goto cleanup;
		}
	}

cleanup:
	munmap(run, mmap_size);
	close(vcpufd);
	close(vmfd);
	close(kvmfd);
	return 0;
}
