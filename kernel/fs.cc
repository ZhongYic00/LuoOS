#include "fs.hh"
#include "vm.hh"
#include "kernel.hh"
// #include "klib.h"

// #define moduleLevel LogLevel::debug

// #define FMT_PROC(fmt,...) Log(info,"Proc[%d]::\n\"\n" fmt "\n\"",kHartObj().curtask->getProcess()->pid(),__VA_ARGS__)
#define FMT_PROC(fmt,...) printf(fmt,__VA_ARGS__)

// @todo error handling
using namespace fs;

xlen_t fs::File::write(xlen_t addr,size_t len){
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.w)return rt;
    auto bytes=kHartObj().curtask->getProcess()->vmar.copyin(addr,len);
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
            if (obj.ep->entWrite(true, addr, off, len) == len) {
                off += len;
                rt = len;
            }
            else { rt = sys::statcode::err; }
        default:
            break;
    }
    return rt;
}
klib::ByteArray fs::File::read(size_t len, long a_off, bool a_update){
    if(a_off < 0) { a_off = off; }
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
            klib::ByteArray buf(len);
            if((rdbytes = obj.ep->entRead(false, (uint64)buf.buff, a_off, len)) > 0) {
                if(a_update) { off = a_off + rdbytes; }
            }
            return klib::ByteArray(buf.buff, rdbytes);
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
            if(ops.fields.r)obj.pipe->decReader();
            else if(ops.fields.w)obj.pipe->decWriter();
            obj.pipe.reset();
            break;
        }
        case FileType::entry: {
            obj.ep->entRelse();
            break;
        }
        case FileType::dev: {
            break;
        }
    }
}


