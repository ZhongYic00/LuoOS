set disassemble-next-line off
layout split
target remote : 1234

b panic
b getDents64
c
c
b vm.hh:278
c