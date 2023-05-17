include common.mk

.DEFAULT_GOAL := all


objdir := obj
depdir := $(objdir)/.deps
$(depdir):
	@mkdir -p $@
depflags = -MMD -MP -MF $(depdir)/$*.d
OS := $(objdir)/os.elf

UCFLAGS = $(CFLAGS) -T user/user.ld
CFLAGS += -Iinclude/ -O0 -g
compile = $(CC) $(depflags) $(CFLAGS)

# SBI: sbi/sbi.elf;
OS: $(OS);
all:  OS #SBI

ksrcs = kernel/start.S\
	$(shell find kernel/ -name "*.cc")
utilsrcs = $(shell find utils/ -name "*.cc")
utilobjs = $(patsubst %.cc,$(objdir)/%.o,$(utilsrcs))
src3party = $(shell find thirdparty/ -name "*.cc")
ksrcs += $(utilsrcs) $(src3party)
kobjs0 = $(patsubst %.S,$(objdir)/%.o,$(ksrcs))
# kobjs1 = $(patsubst %.c,$(objdir)/%.o,$(kobjs0))
kobjs = $(patsubst %.cc,$(objdir)/%.o,$(kobjs0))

depfiles := $(patsubst $(objdir)/%.o,$(depdir)/%.d,$(kobjs))
$(depfiles):
include $(wildcard $(depfiles))
$(info ksrcs=$(ksrcs), kobjs=$(kobjs), depfils=$(depfiles))

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
$(OS): $(kobjs) $(uimg)
	$(info ksrcs=$(ksrcs), kobjs=$(kobjs))
	@echo + CC $<
	$(CC) $(CFLAGS) -T kernel/os.ld -o $(OS) $^

usersrcs = $(shell find user/ -name "*.cc")
userprogs := $(patsubst %.cc,$(objdir)/%.elf,$(usersrcs))
$(info utilobjs=$(utilobjs))
$(objdir)/user/%.elf : user/%.cc $(utilobjs)
	@echo +CC $^
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -o $@ $^
uprogs: $(userprogs)
$(uimg): $(userprogs)
	$(OBJCOPY) -I binary -O elf64-littleriscv --binary-architecture riscv --prefix-sections=uimg $< $@


TEXTMODE=-nographic > obj/output
run: all
	@${QEMU} -M ? | grep virt >/dev/null || exit
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@${QEMU} ${QFLAGS} -kernel $(OS) ${TEXTMODE} 2> obj/log

.PHONY : debug
debug: all
	@echo "Press Ctrl-C and then input 'quit' to exit GDB and QEMU"
	@echo "-------------------------------------------------------"
	@${QEMU} ${QFLAGS} -kernel $(OS) -s -S ${TEXTMODE} 2> obj/log &
	@${GDB} $(OS) -q -x gdbinit && killall ${QEMU}

.PHONY : code
code: all
	@${READELF} -a $(OS) > os-elf.txt
	@${OBJDUMP} -S -D $(OS) >> os-elf.txt
	@less os-elf.txt

.PHONY : clean
clean:
	rm -rf obj/*
	rm -rf *.o *.bin *.elf

.PHONY : stats
stats:
	@echo $(shell find -regex ".*\.[h|c]+" |xargs cat | wc -l) lines