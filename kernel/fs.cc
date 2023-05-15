#include "fs.hh"
#include "vm.hh"
#include "kernel.hh"

#define FMT_PROC(fmt,...) "Proc[%d]::"#fmt"\n",kHartObjs.curtask->getProcess()->pid(),__VA_ARGS__

void fs::File::write(xlen_t addr,size_t len){
    auto bytes=kHartObjs.curtask->getProcess()->vmar.copyin(addr,len);
    switch(type){
        case FileType::stdout:
        case FileType::stderr:
            printf(FMT_PROC("%s",bytes.c_str()));
            break;
        case FileType::pipe:
            obj.pipe->write(bytes);
            break;
        default:
            break;
    }
}

void fs::File::fileClose(){
    --ref;
    if(ref <= 0){
        /*
            switch(type){
                对管道、设备等的处理
            }
        */
        ref = 0;
        type = none;
        // 需不需要delete等操作?
    }
    return;
}