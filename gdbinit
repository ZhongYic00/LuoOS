set disassemble-next-line off
layout split
b _start
b sbi_init
target remote : 1234
c
