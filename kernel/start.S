.equ	STACK_SIZE, 1024
#define MAXNUM_CPU 8
.text
.global _start
_start:
    la sp,kstack + STACK_SIZE
    call start_kernel

.bss
kstack:
	.skip	STACK_SIZE * MAXNUM_CPU # allocate space for all the harts stacks