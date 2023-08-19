define loadsymbol
	add-symbol-file obj/testsuit/sdcard/$arg0
end
define logoutput
    set enableLevel=$arg0
    set kLogger.outputLevel=$arg0
end

define gotoUmode
	b _strapexit:umodeEntry
	c
	delete breakpoints
	advance *$sepc
end

directory obj/testsuit/busybox
directory obj/testsuit/musl/riscv-musl
directory obj/testsuit/libc-test/src

set disassemble-next-line off
layout split
target remote : 1234

b panic
