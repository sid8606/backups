/*
 * guest.S - Minimal AArch64 guest payload
 *
 * Behavior:
 *   - Set up a small stack in low RAM
 *   - Call a print routine that writes bytes to MMIO_ADDR (0x10000000)
 *     Each byte written to the unmapped MMIO triggers KVM_EXIT_MMIO and
 *     the host userspace prints the character.
 *   - Execute HLT to signal exit (KVM_EXIT_HLT).
 *
 * Link address:
 *   - Linked at 0x40000000 by guest.ld
 *
 * Assemble with:
 *   as -o guest.o guest.S
 * Link with guest.ld and objcopy to produce guest.bin
 */

        .section .text
        .global _start

_start:
        /* Place stack pointer somewhere within the RAM at 0x10000 */
        ldr     x0, =0x10000
        mov     sp, x0

        /* x0 = pointer to message */
        adr     x0, message
        bl      print_string

        /* Halt the CPU so the host observes KVM_EXIT_HLT */
        hlt     #0
        b       .

/* print_string:
 *   - x0: pointer to NUL-terminated string
 * Writes each byte to physical address 0x10000000 (MMIO_ADDR) which is
 * not backed by RAM in the userspace program so KVM will exit on each store.
 */
print_string:
        mov     x1, #0          /* index */
1:
        ldrb    w2, [x0, x1]
        cmp     w2, #0
        beq     2f
        ldr     x3, =0x10000000
        strb    w2, [x3]       /* write byte -> causes KVM_EXIT_MMIO in userspace */
        add     x1, x1, #1
        b       1b
2:
        ret

        .align 8
message:
