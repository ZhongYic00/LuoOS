# CROSS_COMPILE = riscv64-linux-gnu-
CROSS_COMPILE = riscv64-unknown-elf-
# CROSS_COMPILE = riscv-nuclei-elf-
CFLAGS = -nostdlib -fno-exceptions -fno-builtin -march=rv64g -mabi=lp64 -mcmodel=medany -Wall -std=c++17

QEMU = qemu-system-riscv64
QFLAGS =  -smp 2 -m 128M -machine virt -bios default -drive file=tests/fat.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
# QFLAGS += -d int,mmu

GDB = gdb-multiarch
CC = ${CROSS_COMPILE}g++
OBJCOPY = ${CROSS_COMPILE}objcopy
OBJDUMP = ${CROSS_COMPILE}objdump
READELF = ${CROSS_COMPILE}readelf