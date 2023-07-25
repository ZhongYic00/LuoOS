set disassemble-next-line off
layout split
target remote : 1234

b panic

b syscall.cc:590
c