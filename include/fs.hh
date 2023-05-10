#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"

namespace fs{
    struct File{
        enum FileType{
            none,pipe,entry,dev
        };
        FileType type;
        union Data
        {
            pipe::Pipe* pipe;
        }obj;
        void write(xlen_t addr,size_t len);
    };
}
#endif