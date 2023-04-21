include common.mk

.DEFAULT_GOAL := all

SBI: sbi/sbi.elf;
OS: kernel/os.elf;
all:  OS SBI

objdir := obj
depdir := $(objdir)/.deps
$(depdir):
	@mkdir -p $@
depflags = -MMD -MP -MF $(depdir)/$*.d
depfiles := $(patsubst %.c,$(depdir)/%.d,$(ksrcs))

UCFLAGS = $(CFLAGS) -T user/user.ld
CFLAGS += -Iinclude/ -O0 -g
compile = $(CC) $(depflags) $(CFLAGS)

$(depfiles):
include $(wildcard $(depfiles))

ksrcs = kernel/start.S\
	$(shell find kernel/ -name "*.cc")
utilsrcs = $(shell find utils/ -name "*.cc")
src3party = $(shell find thirdparty/ -name "*.cc")
ksrcs += $(utilsrcs) $(src3party)
kobjs := $(patsubst %.%,$(objdir)/%.o,$(ksrcs))

$(objdir)/%.o : %.cc
		@echo + CC $<
		@mkdir -p $(dir $(depdir)/$*.d)
		@mkdir -p $(dir $@)
		$(compile) -c -o $@ $<
$(objdir)/%.o : %.S
		@echo + CC $<
		@mkdir -p $(dir $(depdir)/$*.d)
		@mkdir -p $(dir $@)
		$(compile) -c -o $@ $<
uimg = $(objdir)/user/uimg.o
kernel/os.elf: $(kobjs) $(uimg)
	$(info ksrcs=$(ksrcs), kobjs=$(kobjs))
	@echo + CC $<
	$(CC) $(CFLAGS) -T kernel/os.ld -o kernel/os.elf $^

usersrcs = $(shell find user/ -name "*.cc")
userprogs := $(patsubst %.cc,$(objdir)/%.elf,$(usersrcs))
$(objdir)/user/%.elf : user/%.cc
	@echo +CC $<
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -o $@ $<
uprogs: $(userprogs)
$(uimg): $(userprogs)
	$(OBJCOPY) -I binary -O elf64-littleriscv --binary-architecture riscv --prefix-sections=uimg $< $@

run: all
	@${QEMU} -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@${QEMU} ${QFLAGS} -kernel kernel/os.elf 2>log

.PHONY : debug
debug: all
	@echo "Press Ctrl-C and then input 'quit' to exit GDB and QEMU"
	@echo "-------------------------------------------------------"
	@${QEMU} ${QFLAGS} -kernel kernel/os.elf -s -S 2> log &
	@${GDB} kernel/os.elf -q -x gdbinit && killall ${QEMU}

.PHONY : code
code: all
	@${READELF} -a kernel/os.elf > os-elf.txt
	@${OBJDUMP} -S -D kernel/os.elf >> os-elf.txt
	@less os-elf.txt

.PHONY : clean
clean:
	rm -rf *.o *.bin *.elf

.PHONY : stats
stats:
	@echo $(shell find -regex ".*\.[h|c]+" |xargs cat | wc -l) lines