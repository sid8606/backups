.section ".init"
.global _start

_start:
	ldr x0, =0x4020000
	mov sp, x0
	bl main
	b .
