include ../common.mk

SRCS_ASM = \
	start.S \

SRCS_C = \
	$(shell find -name "*.cc") ../klib.cc
$(info SRCS_C=${SRCS_C})

OBJS = $(SRCS_ASM:.S=.o)
OBJS += $(SRCS_C:.c=.o)

.DEFAULT_GOAL := all
all: os.elf

# start.o must be the first in dependency!
os.elf: ${OBJS}
	${CC} ${CFLAGS} -T os.ld -o os.elf $^
	# ${OBJCOPY} -O binary os.elf os.bin

%.o : %.c
	${CC} ${CFLAGS} -c -o $@ $<

%.o : %.S
	${CC} ${CFLAGS} -c -o $@ $<

.PHONY : code
code: all
	@${OBJDUMP} -S -D os.elf > os-elf.txt
	@less os-elf.txt

.PHONY : clean
clean:
	rm -rf *.o *.bin *.elf

