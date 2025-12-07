# Detailed steps — what each `ioctl` call does in `kvm.c`

This document explains, in order of appearance in the `kvm.c` minimal AArch64 userspace KVM example, what each `ioctl` call (and related action) does, the important fields and common pitfalls. It is written so you can read and reuse the explanations while developing or debugging a minimal KVM-based VMM.

---

## Table of contents
1. `KVM_GET_API_VERSION`
2. `KVM_CREATE_VM`
3. `KVM_SET_USER_MEMORY_REGION`
4. `KVM_CREATE_VCPU`
5. `KVM_ARM_PREFERRED_TARGET`
6. `KVM_ARM_VCPU_INIT`
7. `KVM_SET_ONE_REG`
8. `KVM_GET_VCPU_MMAP_SIZE`
9. `mmap` of `kvm_run` region (on vcpu fd)
10. `KVM_RUN`

Each section contains:
- The exact `ioctl` signature as used from userspace
- What the kernel expects and returns
- The effect on KVM internal state
- Important struct fields and how they are used
- Common errors and troubleshooting
- Notes specific to AArch64 where relevant

---

## 1) `ioctl(kvmfd, KVM_GET_API_VERSION, 0)`

**Purpose**
- Query the KVM kernel module for the userspace API version. This ensures the program uses a compatible KVM ABI.

**Signature (userspace)**
```c
int api = ioctl(kvmfd, KVM_GET_API_VERSION, 0);
```

**Kernel-side**
- Returns an `int` (e.g., `KVM_API_VERSION`) defined in `<linux/kvm.h>`.
- If the ioctl is unsupported or `/dev/kvm` is invalid, it returns `-1` and sets `errno`.

**Effect**
- No persistent state in the kernel; pure query.
- You should check the returned value equals `KVM_API_VERSION` (generally `12` or so depending on kernel headers).

**Common pitfalls**
- Not checking the return value: different kernel and userspace headers can be incompatible.
- Running on a kernel that doesn't support KVM (no `/dev/kvm`), which leads to `EBADF` or `ENOTTY`.

---

## 2) `ioctl(kvmfd, KVM_CREATE_VM, (unsigned long)0)`

**Purpose**
- Create a new VM (guest) context managed by KVM in the kernel and return a new file descriptor representing that VM.

**Signature**
```c
int vmfd = ioctl(kvmfd, KVM_CREATE_VM, (unsigned long)0);
```

**Kernel-side**
- Allocates a `struct kvm` and associated kernel resources for the VM.
- Returns a file descriptor (`vmfd`) that userspace can use with further VM-specific ioctls.

**Effect**
- Establishes a VM namespace. Memory regions, devices, vCPUs are associated with this VM via `vmfd`.

**Important notes**
- `vmfd` is an independent FD. Closing it destroys VM resources (if no other refs).
- Some architectures require additional ioctls to enable specific features (e.g., GIC setup on ARM).

**Common errors**
- `EINVAL` if arguments invalid (rare here).
- `ENOMEM` if kernel cannot allocate resources.
- `EBUSY` if KVM is not available.

---

## 3) `ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region)`

**Purpose**
- Register a userspace memory range as guest physical memory for the VM. KVM will map guest physical addresses to the provided userspace address.

**Signature**
```c
struct kvm_userspace_memory_region region;
memset(&region,0,sizeof(region));
region.slot = slot;
region.flags = readonly ? KVM_MEM_READONLY : 0;
region.guest_phys_addr = guest_phys;
region.memory_size = mem_size;
region.userspace_addr = (uint64_t)user_mem;
ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
```

**Key fields**
- `slot` — an arbitrary integer used to identify this memory window. You can later replace/remove regions by reusing the slot.
- `guest_phys_addr` — base guest physical address mapped to `userspace_addr`.
- `memory_size` — length in bytes of region (must be page aligned in many kernels).
- `userspace_addr` — host virtual address of the mapped region (cast to `uint64_t`).

**Kernel-side**
- KVM adds this region to the VM's guest phys map and will translate guest physical accesses into host virtual accesses when the vCPU runs.

**Effect**
- Guest reads/writes to `[guest_phys_addr .. guest_phys_addr + memory_size)` are backed by the specified `userspace_addr` memory.

**Pitfalls and notes**
- `userspace_addr` must be a valid pointer in the process that performs the ioctl. KVM accesses this memory directly (zero-copy).
- `mmap` with `PROT_READ|PROT_WRITE` and `MAP_SHARED` is common.
- If `memory_size` isn't page-aligned or `userspace_addr` is not properly aligned, the kernel may return `EINVAL`.
- Overlapping regions, or conflicting guest addresses, cause undefined behavior or `EINVAL`.
- `KVM_MEM_READONLY` flag restricts guest writes; attempting to write may cause an exit or fault.
- Use `slot` carefully to replace/unmap regions.

**Troubleshooting**
- Failure often shows `EINVAL` or `EFAULT`. Check alignment and that `userspace_addr` is in the calling process address space.

---

## 4) `ioctl(vmfd, KVM_CREATE_VCPU, 0)`

**Purpose**
- Create a vCPU associated with the VM. Returns a file descriptor representing the vCPU.

**Signature**
```c
int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
```

**Kernel-side**
- Allocates kernel-side vCPU state and returns a vCPU FD (`vcpufd`). This FD is used for vCPU-specific ioctls (init, run, set regs).

**Effect**
- Registers another virtual CPU which can be run via `KVM_RUN`. Multiple vCPUs are independent file descriptors.

**Important**
- The number of vCPUs supported may be limited by kernel config or resources.
- The `arg` parameter is typically the vCPU id for some hypervisors but for KVM the call usually takes an index or 0; use kernel headers to know specifics.

**Errors**
- `ENOSPC`/`ENOMEM` on resource exhaustion.

---

## 5) `ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, &target)`

**Purpose**
- Obtain a recommended `kvm_vcpu_init` configuration for the ARM target (vCPU model, features).

**Signature**
```c
struct kvm_vcpu_init target;
ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, &target);
```

**Kernel-side**
- Fills `target` with values (e.g., `target.target`, and `target.features` bitmask) that userspace should use when calling `KVM_ARM_VCPU_INIT`.

**Effect**
- Ensures the vCPU is initialized with a set of CPU features compatible with the host kernel and hardware.

**Notes**
- On some kernels/architectures, this ioctl may be required before `KVM_ARM_VCPU_INIT`.
- The returned `target` may include `features` to enable GICv4, PSCI, etc.

---

## 6) `ioctl(vcpufd, KVM_ARM_VCPU_INIT, &target)`

**Purpose**
- Initialize vCPU architecture-specific state using the `target` returned previously. This configures registers, system registers, and other architecture-dependent items.

**Signature**
```c
ioctl(vcpufd, KVM_ARM_VCPU_INIT, &target);
```

**Effect**
- Kernel creates initial CPU system register state, and configures the vCPU according to `target`.

**Pitfalls**
- If the `target` is incompatible or invalid, kernel returns `EINVAL`.
- Must be performed before running vCPU or setting many system registers.

---

## 7) `ioctl(vcpufd, KVM_SET_ONE_REG, &reg)`

**Purpose**
- Set an individual vCPU register (or system register) identified by `reg.id`. This is the modern way to set CPU registers; older APIs used grouped regs.

**Signature**
```c
struct kvm_one_reg reg;
reg.id = id;           /* composed using macros: KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | index */
reg.addr = (uint64_t)&pc_copy; /* userspace pointer to value */
ioctl(vcpufd, KVM_SET_ONE_REG, &reg);
```

**How `id` is composed (ARM64)**
- Use macros in `<linux/kvm.h>`:
  - `KVM_REG_ARM64`, `KVM_REG_SIZE_U64`, `KVM_REG_ARM_CORE` and an index that indexes 32-bit words of the `struct kvm_regs` area.
- In the example `set_pc()` computes `index = offsetof(struct kvm_regs, regs.pc) / sizeof(__u32)`.

**Kernel-side**
- Copies the value from `reg.addr` (userspace pointer) into the identified register in kernel vCPU state.

**Notes**
- `reg.addr` points to a value in userspace; kernel will copy the value at that address. Ensure it remains valid when call made.
- For wide registers or system registers, ensure `KVM_REG_SIZE_*` matches the size of the data.
- Wrong `id` yields `EINVAL` or `EFAULT`.

**Common mistakes**
- Passing an address of a temporary variable on stack and then reusing it later — but since `ioctl` is synchronous, this is normally fine.
- Not using the correct `index` leading to wrong register being set.

---

## 8) `ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, 0)`

**Purpose**
- Ask KVM how large the shared `kvm_run` structure is for the vCPU mapping. The returned size is used when mapping the `kvm_run` shared memory region.

**Signature**
```c
int mmap_size = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, 0);
```

**Kernel-side**
- Returns the mmapped region size in bytes. This includes `sizeof(struct kvm_run)` plus any extra per-vCPU extensions required by the kernel (like MMIO data, embedded exit structures, etc).

**Notes**
- Always check the returned size > 0.
- Different kernels/architectures yield different sizes.

---

## 9) `mmap(NULL, mmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, vcpufd, 0)`

**Purpose**
- Map the kernel-provided `kvm_run` shared structure into userspace for the vCPU. This is not an ioctl but is required to interact with vCPU exits.

**Effect**
- Produces a pointer `struct kvm_run *run` that the userspace program reads after `KVM_RUN` returns (or when `KVM_RUN` returns due to exit).
- The kernel writes exit information into this structure.

**Pitfalls**
- Failing to map with the returned `mmap_size` may cause truncated data.
- Accessing fields in `run` before `KVM_RUN` returns is meaningless.

---

## 10) `ioctl(vcpufd, KVM_RUN, 0)`

**Purpose**
- Execute the vCPU: switch into guest context and run until the VM performs an operation that requires userspace handling (e.g., MMIO, IO ports, halt, shutdown), or a signal/interrupt occurs.

**Signature**
```c
ret = ioctl(vcpufd, KVM_RUN, 0);
```

**Kernel-side**
- Performs a long blocking call that enters the guest and returns only when there is an exit (or error).
- On return, the `kvm_run` shared struct is populated with `exit_reason` and relevant fields for that exit.

**Common exit reasons**
- `KVM_EXIT_MMIO` — guest performed MMIO (read/write) to an address not backed by `KVM_SET_USER_MEMORY_REGION` -> handle in userspace (the `kvm_run` struct contains `mmio` details).
- `KVM_EXIT_HLT` — guest executed `hlt` (or equivalent).
- `KVM_EXIT_SHUTDOWN` — guest requested shutdown (e.g., PSCI call).
- `KVM_EXIT_EXCEPTION` — guest trapped to exception state.
- Others: `INTERNAL_ERROR`, `EXIT_IO`, etc.

**Important behavior**
- `KVM_RUN` can return `-1` and set `errno == EINTR` if the process receives an interrupt signal; recommended to loop and retry.
- `KVM_RUN` changes the kernel's vCPU state and may update internal timing or injected interrupts.

**Pitfalls and best practices**
- `KVM_RUN` is blocking; design your program to handle signals cleanly.
- After an exit, examine `run->exit_reason` and consume any data (e.g., `run->mmio.data`) before the next `KVM_RUN`.
- For MMIO write exits, the data is available in `run->mmio.data` (host-visible). For reads, userspace must fill the `data` area (depending on MMIO protocol) before resuming.

---

## Extra notes: memory layout and MMIO

- In this example `MMIO_ADDR` (0x10000000) is intentionally left unmapped from userspace via `KVM_SET_USER_MEMORY_REGION`. When the guest performs a store to that address, the kernel triggers `KVM_EXIT_MMIO` and provides the written bytes to userspace in the `kvm_run` mmio fields. This is a common way to implement a simple UART or console device in a minimal VMM.

- Keep `userspace_addr` valid for the lifetime of its registration. If you `munmap()` or free it, you must first unregister or replace it via `KVM_SET_USER_MEMORY_REGION` (with memory_size=0 to remove) or close the VM.

---

## Troubleshooting common ioctl errors

- **`EINVAL`** — invalid arguments, often due to wrong sizes or misaligned addresses (e.g., mapping unaligned memory_size).
- **`EFAULT`** — bad userspace pointer passed (e.g., `userspace_addr` not mapped in process).
- **`EBUSY` / `ENODEV`** — KVM not available or already in a conflicting state.
- **`EPERM`** — insufficient permissions for `/dev/kvm`.
- **`ENOSPC` / `ENOMEM`** — kernel ran out of memory or resources when creating VM or vCPU.

---

## Minimal runtime check-list (order of operations)

1. `open("/dev/kvm", O_RDWR)` — must succeed.
2. `ioctl(kvmfd, KVM_GET_API_VERSION, 0)` — verify returned version.
3. `ioctl(kvmfd, KVM_CREATE_VM, 0)` — get `vmfd`.
4. `mmap()` userspace memory regions and call `KVM_SET_USER_MEMORY_REGION` for each region.
5. `ioctl(vmfd, KVM_CREATE_VCPU, 0)` — get `vcpufd`.
6. `ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, &target)` — fill `target`.
7. `ioctl(vcpufd, KVM_ARM_VCPU_INIT, &target)` — initialize vCPU architecture state.
8. `KVM_SET_ONE_REG` for PC (and any required registers).
9. `ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, 0)` and `mmap` the `kvm_run` region using `vcpufd`.
10. Loop on `ioctl(vcpufd, KVM_RUN, 0)`, handle `run->exit_reason`.

---

## Build & run notes

- Build: `gcc -O2 -Wall -o kvm kvm.c`
- Run as root or a user with read/write to `/dev/kvm`:
  ```sh
  sudo ./kvm
  ```

---

## References
- `<linux/kvm.h>` — KVM ioctl and structure definitions.
- Kernel documentation under `Documentation/virt/kvm` (or online kernel docs) for architecture-specific notes (ARM64).
- Examples in QEMU and small-kvm projects for idiomatic usage.

---

*End of file*
