#include "fs.hh"
#include "vm.hh"
#include "kernel.hh"

#define FMT_PROC(fmt,...) Log(info,"Proc[%d]::\n\"\n" fmt "\n\"",kHartObjs.curtask->getProcess()->pid(),__VA_ARGS__)

// @todo error handling
void fs::File::write(xlen_t addr,size_t len){
    if(!ops.fields.w)return ;
    auto bytes=kHartObjs.curtask->getProcess()->vmar.copyin(addr,len);
    switch(type){
        case FileType::stdout:
        case FileType::stderr:
            FMT_PROC("%s",bytes.c_str());
            break;
        case FileType::pipe:
            obj.pipe->write(bytes);
            break;
        default:
            break;
    }
}
void fs::File::read(xlen_t addr,size_t len){
    if(!ops.fields.r)return ;
    switch(type){
        case FileType::stdin:
            panic("unimplementd!");
            break;
        case FileType::pipe:
            {auto bytes=obj.pipe->read(len);
            kHartObjs.curtask->getProcess()->vmar[addr]=bytes;}
            break;
        default:
            break;
    }
}