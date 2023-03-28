include common.mk

.DEFAULT_GOAL := all
all: kernel/os.elf sbi/sbi.elf

# start.o must be the first in dependency!

run: all
	@${QEMU} -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@${QEMU} ${QFLAGS} -kernel kernel/os.elf

.PHONY : debug
debug: all
	@echo "Press Ctrl-C and then input 'quit' to exit GDB and QEMU"
	@echo "-------------------------------------------------------"
	@${QEMU} ${QFLAGS} -kernel kernel/os.elf -s -S &
	@${GDB} kernel/os.elf -q -x gdbinit && killall ${QEMU}

.PHONY : code
code: all
	@${OBJDUMP} -S -D os.elf > os-elf.txt
	@less os-elf.txt

.PHONY : clean
clean:
	rm -rf *.o *.bin *.elf

