#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"
#include "resmgr.hh"

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
    using pipe::Pipe;
    struct File{
        enum FileType{
            none,pipe,entry,dev,inode,
            stdin,stdout,stderr
        };
        const FileType type;
        union Data
        {
            sharedptr<Pipe> pipe;
            sharedptr<INode> inode;
            Data(FileType type){
                switch(type){
                    case FileType::pipe:pipe=make_shared<Pipe>();break;
                    default:;
                }
            }
            ~Data(){}
        }obj;
        File(FileType type):type(type),obj(type){
        }
        ~File(){
            switch(type){
                case FileType::pipe:obj.pipe.~sharedptr();break;
                case FileType::inode:obj.inode.~sharedptr();break;
            }
        }
        void write(xlen_t addr,size_t len);
        void fileClose();
    };
}
#endif