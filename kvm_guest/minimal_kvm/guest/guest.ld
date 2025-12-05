/* guest.ld - Linker script placing guest at 0x40000000
 *
 * This script places .text at 0x40000000 so the flat binary produced
 * by objcopy can be loaded at that physical address in the guest.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}
