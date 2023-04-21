set disassemble-next-line off
layout split
add-symbol-file sbi/sbi.elf 0x80000000
add-symbol-file obj/user/prog0.elf 0x83000000
target remote : 1234

