set disassemble-next-line on
layout split
b _start
b sbi_init
target remote : 1234
c
