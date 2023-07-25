set disassemble-next-line off
layout split
target remote : 1234

b panic

b syscall.cc:sendFile
c