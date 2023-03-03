##include "platform.h"
#define MAXNUM_CPU 8

	# size of each hart's stack is 1024 bytes
	.equ	STACK_SIZE, 1024

	.global	_start
    .extern trapvector

	.text
_start:
	# park harts with id != 0
	csrr	t0, mhartid		# read current hart id
	mv	tp, t0			# keep CPU's hartid in its tp for later usage.
	bnez	t0, park		# if we're not on the hart 0
					# we park the hart
	# Setup stacks, the stack grows from bottom to top, so we put the
	# stack pointer to the very end of the stack range.
	slli	t0, t0, 10		# shift left the hart id by 1024
	la	sp, stacks + STACK_SIZE	# set the initial stack pointer
					# to the end of the first stack space
	add	sp, sp, t0		# move the current hart stack pointer
					# to its place in the stack space

    call   sbi_init            # hart 0 jump to sbi
    # la t1, trapvector
    # csrw mtvec,t1
    # la t6, specialstack
    # csrw mscratch,t6
    # j mtraptest1
    # j park

	j	start_kernel		# hart 0 jump to c

park:
	wfi
	j	park
trapvector:
    csrrw	t6, mscratch, t6
    # call mtraphandler
    csrr t6,mepc
    addi t6,t6,4
    csrw mepc,t6
    csrr t6,mscratch
    mret
mtraptest1:
    li t0,0
    sw t0,0(t0)
endless:
    j endless

.data
stacks:
	.skip	STACK_SIZE * MAXNUM_CPU # allocate space for all the harts stacks

	.end				# End of file
