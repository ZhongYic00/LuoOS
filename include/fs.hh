#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"
#include "resmgr.hh"
#include "klib.hh"


namespace fs{
    using klib::SharedPtr;
    using pipe::Pipe;


    struct INode{
        enum INodeType{
            dir, file, dev
        };
        bool m_valid;
        INodeType m_type;
        size_t m_size;
    };

    struct File {
        enum FileType { none,pipe,entry,dev,inode, stdin,stdout,stderr };
        enum class FileOp:uint16_t { none=0,read=0x1,write=0x2,append=0x4, };

        union FileOps {
            struct{
                int r:1;
                int w:1;
                int a:1;
                int gc:1;
                int defer:1;
            }fields;
            FileOp raw;
            FileOps(FileOp a_ops=FileOp::none):raw(a_ops){}
        }ops;
        union Data {
            SharedPtr<Pipe> pipe;
            SharedPtr<INode> inode;
            Data(FileType a_type){ assert(a_type==none || a_type==stdin || a_type==stdout || a_type==stderr); }
            Data(const SharedPtr<Pipe> &a_pipe):pipe(a_pipe){}
            Data(const SharedPtr<INode> &a_inode):inode(a_inode){}
            ~Data(){}
        }obj;
        const FileType type;
        struct dirent *ep;

        File(FileType type,FileOp ops=FileOp::none):type(type),obj(type),ops(ops){}
        File(FileType a_type): type(a_type), obj(a_type) {}
        File(const SharedPtr<Pipe> &a_pipe, FileOp a_ops): type(FileType::pipe), obj(a_pipe), ops(a_ops) {}
        File(FileType a_type, const SharedPtr<INode> &a_inode): type(a_type), obj(a_inode) {}
        ~File();
        void write(xlen_t addr,size_t len);
        void read(xlen_t addr, size_t len);
    };
}
#endif