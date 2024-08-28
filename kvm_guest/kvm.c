#include <stdio.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stddef.h>

struct kvm_userspace_memory_region regions[4];
int regions_created = 0;

//struct kvm_userspace_memory_region has four members.
void new_region(uint32_t flags, uint64_t guest_phys, size_t mem_size, int vm)
{
	regions[regions_created].slot = regions_created;
	regions[regions_created].flags = flags;
	regions[regions_created].guest_phys_addr = guest_phys;
	regions[regions_created].memory_size = mem_size;

	void* user_mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	regions[regions_created].userspace_addr = (uint64_t) user_mem;

	if (ioctl(vm, KVM_SET_USER_MEMORY_REGION, &regions[regions_created]) < 0) {
		printf("Error: region %d failed\n", regions_created);
		return;
	}
	regions_created++;
}

void place_guest_image()
{
	struct stat st;
	int img = 0, size = 0; ;

	img = open("./guest/kernel.bin", O_RDONLY);
	stat("./guest/kernl.bin", &st);
	size = st.st_size;
	read(img, (void*)regions[1].userspace_addr, size);
}

int main()
{
	int ret;

	int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if (kvm < 0 ) {
		printf("Error: opening /dev/kvm\n");
		return 1;
	}

	/*KVM_GET_API_VERSION returns 12. Kernel in 2.6.20 and 2.6.21
	 * report other versions which are not documented.
	 */ 
	int api = ioctl(kvm, KVM_GET_API_VERSION, NULL);
	if (api != 12) {
		printf("Error: API not v12\n");
		return 1;
	}

	/* • Kernel stores state of each guest in struct kvm.
	 * This contains info about VM’s mm, VCPUs, busses, etc.
	 * • Created using KVM_CREATE_VM ioctl.
	 * • Allocates memory for struct kvm its members and in ARM, initialises stage 2page tables.
	 * • Returns file descriptor associated with this VM. VM ioctl is now available.
	 */

	int vm = ioctl(kvm, KVM_CREATE_VM, (unsigned long)0);
	if (vm < 0) {
		printf("Error: KVM_CREATE_VM\n");
		return 1;
	}

	/* • Now that struct kvm is allocated, stage 2 page tables are also initialised.
	 * • Set these page tables from userspace by calling KVM_SET_USER_MEMORY_REGION.
	 */

	new_region(0, 0x0, 0x10000, vm);
	new_region(0, 0x4000000, 0x10000, vm);
	new_region(0, 0x4010000, 0x10000, vm);
	new_region(KVM_MEM_READONLY, 0x10000000, 0x10000, vm);

	place_guest_image();

	/* • While switching context to guest, each virtual processing element’s state is in struct kvm_vcpu.
	 * • Contains information such as:
	 * 	• VM it belongs.
	 * 	• Preempt notifiers.
	 * 	• ID.
	 * 	• kvm_run structure (discussed ahead).
	 * 	• GIC cpu interface state.
	 * 	• Timer state.
	 * 	• GP registers and sysregs state.
	 * • a VIRTUAL processing element needs to be initialised.
	 * • Done using KVM_CREATE_VCPU (vcpu id as param).
	 * • In the kernel:
	 * 	• Allocates kvm_vcpu structure and kvm_run. Links it with VM.
	 * 	• Initialise members such as, vcpu_id, kvm, pid, etc.
	 * 	• Set arch specifics:
	 * 	• Stage 2 MMU.
	 * 	• Timer initial state.
	 * 	• Initialise GIC CPU interface state.
	 * 	• Share structure with EL2 page tables.	
	 */	

	//Create vCPU 
	int vcpu = ioctl(vm, KVM_CREATE_VCPU, 0);
	if (vcpu < 0) {
		printf("Error: KVM_CREATE_VCPU\n");
		return 1;
	}

	// Get Preferred target
	struct kvm_vcpu_init target;
	ret = ioctl(vm, KVM_ARM_PREFERRED_TARGET, &target);

	// Initilisw vCPU to reset state
	ret = ioctl(vcpu, KVM_ARM_VCPU_INIT, &target);
	if (vcpu < 0) {
		printf("Error: KVM_ARM_VCPU_INIT\n");
		return 1;
	}

	/*  KVM_ARM_VCPU_INIT initialises the VCPU state by calling kvm_reset_vcpu() in the kernel.
	 *  • Sets register state to architecture defined reset values.
	 *  • set PC to 0x4000000 (guest RESET)
	 *  • KVM_SET_ONE_REG ioctl.
	 */
	uint64_t index = offsetof(struct kvm_regs, regs.pc) / sizeof(__u32);
	uint64_t id = KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | index;
	uint64_t entry = 0x4000000;

	struct kvm_one_reg pc = { 
		.id = id,
		.addr = (uint64_t) entry
	};

	ret = ioctl(vcpu, KVM_SET_ONE_REG, &pc);

	/* • How to run guest code?
	 * • Kernel shares memory with userspace for communication wrt KVM.
	 * 	• mmap() the vCPU fd.
	 * 	• Size for mmap() told by KVM_GET_VCPU_MMAP_SIZE
	 * 	• Structure of communication, struct kvm_run
	 * 	• If KVM comes back to userspace, reason specified in, run->exit_reason.
	 */ 	

	struct kvm_run *run;
	int kvm_run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, NULL);

	run = (struct kvm_run*) mmap(NULL, kvm_run_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu, 0);

	int exit = 0;

	while(!exit) {

		ret = ioctl(vcpu, KVM_RUN, NULL);

		switch(run->exit_reason) {
			case KVM_EXIT_MMIO:
				if(run->mmio.is_write) {
					for (int j = 0; j < run->mmio.len; j++) {
						printf("%c", run->mmio.data[j]);
					}
				}
				break;

			case KVM_EXIT_SYSTEM_EVENT:
				exit = 1;
				break;

			default:
				printf("KVM_RUN weird exit\n");
				break;
		}
	}
}
