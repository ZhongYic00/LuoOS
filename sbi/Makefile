include ../common.mk

SRCS_ASM = \
	start.S \

SRCS_C = \
	$(shell find -name "*.cc") ../utils/klib.cc
$(info SRCS_C=${SRCS_C})

CFLAGS += -I../include

OBJS = $(SRCS_ASM:.S=.o)
OBJS += $(SRCS_C:.c=.o)

.DEFAULT_GOAL := all
all: sbi.elf

# start.o must be the first in dependency!
sbi.elf: ${OBJS}
	${CC} ${CFLAGS} -T sbi.ld -o sbi.elf $^
	# ${OBJCOPY} -O binary sbi.elf sbi.bin

%.o : %.c
	${CC} ${CFLAGS} -c -o $@ $<

%.o : %.S
	${CC} ${CFLAGS} -c -o $@ $<

run: all
	@${QEMU} -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@${QEMU} ${QFLAGS} -kernel sbi.elf

.PHONY : debug
debug: all
	@echo "Press Ctrl-C and then input 'quit' to exit GDB and QEMU"
	@echo "-------------------------------------------------------"
	@${QEMU} ${QFLAGS} -kernel sbi.elf -s -S &
	@${GDB} sbi.elf -q -x gdbinit && killall ${QEMU}

.PHONY : code
code: all
	@${OBJDUMP} -S -D sbi.elf > sbi.elf.txt
	@less sbi.elf.txt

.PHONY : clean
clean:
	rm -rf *.o *.bin *.elf

