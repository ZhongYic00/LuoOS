#define	STACK_SIZE 4096
#define MAXNUM_CPU 8
# .extern kstack_end
.text
.global _start
_start:
    la sp,kstack_start + STACK_SIZE
    call start_kernel

.section .kstack
kstack_start:
    .skip STACK_SIZE * MAXNUM_CPU