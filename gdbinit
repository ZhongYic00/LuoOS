set disassemble-next-line off
layout split
target remote : 1234
add-symbol-file busybox
b panic