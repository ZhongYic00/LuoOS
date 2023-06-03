# CROSS_COMPILE = riscv64-linux-gnu-
# CROSS_COMPILE = riscv64-unknown-elf-
CROSS_COMPILE = riscv-nuclei-elf-
CFLAGS = -nostdlib -fno-exceptions -fno-builtin -march=rv64ima -mabi=lp64 -mcmodel=medany -Wall

QEMU = qemu-system-riscv64
QFLAGS =  -smp 1 -m 128M -machine virt -bios default -d int,mmu 

GDB = gdb-multiarch
CC = ${CROSS_COMPILE}g++
OBJCOPY = ${CROSS_COMPILE}objcopy
OBJDUMP = ${CROSS_COMPILE}objdump
READELF = ${CROSS_COMPILE}readelf