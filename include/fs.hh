#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"

namespace fs{
    struct INode{
        enum INodeType{
            dir, file, dev
        };
        int valid;
        xlen_t ref;
        INodeType type;
        xlen_t size;
        //xlen_t addrs[];
    };
    struct File{
        enum FileType{
            none,pipe,entry,dev,inode,
            stdin,stdout,stderr
        };
        FileType type;
        INode *in;
        xlen_t ref;
        union Data
        {
            pipe::Pipe* pipe;
        }obj;
        File(FileType ftype=none, INode *inptr=nullptr): type(ftype), in(inptr), ref(0) {};
        void write(xlen_t addr,size_t len);
        void fileClose();
    };
}
#endif