set disassemble-next-line off
layout split
target remote : 1234

b panic
b ipc.cc:253
c
b trap.cc:223
c
set enableLevel=1
set kLogger.outputLevel=1
delete breakpoints
add-symbol-file obj/testsuit/sdcard/entry-dynamic.exe
add-symbol-file obj/testsuit/sdcard/libc.so 0x70014250
directory obj/testsuit/libc-test
directory obj/testsuit/musl/riscv-musl
b *0x7005f8bc
