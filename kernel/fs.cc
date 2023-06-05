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
        case FileType::entry:
            elock(obj.ep);
            if (ewrite(obj.ep, 1, addr, off, len) == len) {
                off += len;
                rt = len;
            }
            else { rt = sys::statcode::err; }
            eunlock(obj.ep);
        default:
            break;
    }
    return rt;
}
klib::ByteArray fs::File::read(size_t len){
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.r) { return rt; }
    switch(type){
        case FileType::stdin:
            panic("unimplementd!");
            break;
        case FileType::pipe:
            return obj.pipe->read(len);
            break;
        case FileType::dev:
            panic("unimplementd!");
            break;
        case FileType::entry: {
            int rdbytes = 0;
            klib::ByteArray buf(len); // @todo 第二次execve的时候buf分到了空指针
            elock(obj.ep);
            if((rdbytes = eread(obj.ep, 0, (uint64)buf.buff, off, len)) > 0) { off += rdbytes; }
            eunlock(obj.ep);
            return buf;
            break;
        }
        default:
            panic("File::read(): unknown file type");
            break;
    }
    return klib::ByteArray{0};
}
klib::ByteArray fs::File::readAll(){
    switch(type){
        case FileType::entry:{
            size_t size=obj.ep->file_size;
            return read(size);
        }
        default:
            panic("readAll doesn't support this type");
    }
}

fs::File::~File() {
    switch(type){
        case FileType::pipe: {
            obj.pipe.deRef();
            break;
        }
        case FileType::entry: {
            eput(obj.ep);
            break;
        }
        case FileType::dev: {
            break;
        }
    }
}


