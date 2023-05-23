#include "fs.hh"
#include "vm.hh"
#include "kernel.hh"
// #include "klib.h"

// #define moduleLevel LogLevel::debug

// #define FMT_PROC(fmt,...) Log(info,"Proc[%d]::\n\"\n" fmt "\n\"",kHartObjs.curtask->getProcess()->pid(),__VA_ARGS__)
#define FMT_PROC(fmt,...) printf(fmt,__VA_ARGS__)

// @todo error handling
using namespace fs;

xlen_t fs::File::write(xlen_t addr,size_t len){
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.w)return rt;
    auto bytes=kHartObjs.curtask->getProcess()->vmar.copyin(addr,len);
    Log(debug,"write(%d bytes)",bytes.len);
    switch(type){
        case FileType::stdout:
        case FileType::stderr:
            FMT_PROC("%s",klib::string(bytes.c_str(),len).c_str());
            rt=bytes.len;
            break;
        case FileType::pipe:
            obj.pipe->write(bytes);
            rt=bytes.len;
            break;
        default:
            break;
    }
    return rt;
}
xlen_t fs::File::read(xlen_t addr,size_t len){
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.r)return rt;
    switch(type){
        case FileType::stdin:
            panic("unimplementd!");
            break;
        case FileType::pipe:
            {
                auto bytes=obj.pipe->read(len);
                kHartObjs.curtask->getProcess()->vmar[addr]=bytes;
                rt=bytes.len;
            }
            break;
        default:
            break;
    }
    return rt;
}

fs::File::~File() {
    switch(type){
        case FileType::pipe: {
            obj.pipe.deRef();
            break;
        }
        case FileType::inode: {
            obj.inode.deRef();
            /*关闭逻辑*/
            break;
        }
    }
}


