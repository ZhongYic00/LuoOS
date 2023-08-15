set disassemble-next-line off
layout split
target remote : 1234

b panic
b execve
c
c
set enableLevel=1
set kLogger.outputLevel=1
b trap.cc:222
c 
delete breakpoints
add-symbol-file obj/testsuit/sdcard/entry-dynamic.exe
add-symbol-file obj/testsuit/sdcard/libc.so 0x70014250
directory obj/testsuit/libc-test
directory obj/testsuit/musl/riscv-musl
b pthread_cancel