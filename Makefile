include common.mk

.DEFAULT_GOAL := all


objdir := obj
depdir := $(objdir)/.deps
$(depdir):
	@mkdir -p $@
depflags = -MMD -MP -MF $(depdir)/$*.d
OS := $(objdir)/os.elf

UCFLAGS = $(CFLAGS) -T user/user.ld
# linuxheaders = /usr/src/linux-headers-$(shell uname -r)/include/
CFLAGS += -Iinclude/ -Ithirdparty/tinystl/include -Ithirdparty/eastl-port/include -Ithirdparty/eastl-port/test/packages/EABase/include/Common -O0 -g
compile = $(CC) $(depflags) $(CFLAGS)

# SBI: sbi/sbi.elf;
OS: $(OS);
all:  OS #SBI
	cp $(OS) kernel-qemu

ksrcs = kernel/start.S\
	$(shell find kernel/ -name "*.cc")
utilsrcs = $(shell find utils/ -name "*.cc")
utilobjs = $(patsubst %.cc,$(objdir)/%.o,$(utilsrcs))
src3party = $(shell find thirdparty/ -name "*.cc")
eastl-srcs = $(wildcard thirdparty/eastl-port/source/*.cpp)
ksrcs += $(utilsrcs) $(src3party) $(eastl-srcs)

kobjs = $(addprefix $(objdir)/,$(addsuffix .o,$(basename $(ksrcs))))

depfiles := $(patsubst $(objdir)/%.o,$(depdir)/%.d,$(kobjs))
$(depfiles):
include $(wildcard $(depfiles))
$(info ksrcs=$(ksrcs), kobjs=$(kobjs), depfils=$(depfiles))


$(objdir)/%.o : %.cpp
		@echo + CC $<
		@mkdir -p $(dir $(depdir)/$*.d)
		@mkdir -p $(dir $@)
		$(compile) -c -o $@ $<
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
# userprogs := $(patsubst %.cc,$(objdir)/%.elf,$(usersrcs))
# userprogs := obj/riscv64/yield
userprogs := obj/user/init.elf
# userprogs := user/testsuits/clone
$(info utilobjs=$(utilobjs))
$(objdir)/user/%.elf : user/%.cc obj/utils/klibc.o
	@echo +CC $^
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -o $@ $^
uprogs: $(userprogs)
$(uimg): $(userprogs)
	$(OBJCOPY) -I binary -O elf64-littleriscv --binary-architecture riscv --prefix-sections=uimg $< $@


# TEXTMODE=-nographic > obj/output
run: all
	@echo "Press Ctrl-A and then X to exit QEMU"
	@echo "------------------------------------"
	@${QEMU} ${QFLAGS} -kernel $(OS) -s ${TEXTMODE} 2> obj/log

testrun: all
	rm -rf obj/*
	rm -rf *.o *.bin *.elf
	qemu-system-riscv64 -machine virt -kernel kernel-qemu -m 128M -nographic -smp 2 -bios default -drive file=fat32.img,if=none,format=raw,id=x0  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

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
	@echo $(shell find -regex ".*\.[h|c|S]+" | grep -v thirdparty |xargs cat | wc -l) lines