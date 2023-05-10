#include "fs.hh"
#include "vm.hh"

void fs::File::write(xlen_t addr,size_t len){
    auto bytes=vm::copyin(addr,len);
    switch(type){
        case FileType::pipe:
            obj.pipe->write(bytes);
            break;
        default:
            break;
    }
}