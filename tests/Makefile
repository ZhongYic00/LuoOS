# include ../common.mk
CC = g++
CFLAGS = -DGUEST -g

INC := -I../include

OBJS = $(SRCS_ASM:.S=.o)
OBJS += $(SRCS_C:.c=.o)

.DEFAULT_GOAL := all
all: os.elf

# start.o must be the first in dependency!
vmtest: vmtest.o
	${CC} ${CFLAGS} -o $@ $^
	./vmtest
vmtest-layout: vmtest.cc
	$(CC) -fdump-class-hierarchy $^
alloc: alloc.o tlsf.o
	${CC} ${CFLAGS} -o $@ $^
	./alloc || echo 'failed'

tlsf-src := tlsf.cc

%.elf : %.cc
	${CC} ${CFLAGS} $(INC) -o $@ $<

%.o : %.S
	${CC} ${CFLAGS} $(INC) -c -o $@ $<

.PHONY : code
code: all
	@${OBJDUMP} -S -D os.elf > os-elf.txt
	@less os-elf.txt

.PHONY : clean
clean:
	rm -rf *.o *.bin *.elf

