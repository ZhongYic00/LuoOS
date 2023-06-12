# CROSS_COMPILE = riscv64-linux-gnu-
CROSS_COMPILE = riscv64-unknown-elf-
# CROSS_COMPILE = riscv-nuclei-elf-
CFLAGS = -nostdlib -fno-exceptions -fno-builtin -march=rv64g -mabi=lp64 -mcmodel=medany -Wall

QEMU = qemu-system-riscv64
QFLAGS =  -smp 2 -m 128M -machine virt -bios default -d int,mmu -nographic -drive file=fat32.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

GDB = gdb-multiarch
CC = ${CROSS_COMPILE}g++
OBJCOPY = ${CROSS_COMPILE}objcopy
OBJDUMP = ${CROSS_COMPILE}objdump
READELF = ${CROSS_COMPILE}readelf