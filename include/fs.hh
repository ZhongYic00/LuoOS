#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"
#include "resmgr.hh"
#include "klib.hh"
#include "fcntl.h"


namespace fs{
    using klib::SharedPtr;
    using pipe::Pipe;

    struct dirent;

    struct INode{
        enum INodeType{
            dir, file, dev
        };
        bool m_valid;
        INodeType m_type;
        size_t m_size;
    };

    struct File {
        enum FileType { none,pipe,entry,dev, stdin,stdout,stderr };
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
            FileOps(int a_flags) {
                fields.r = !(a_flags & O_WRONLY);
                fields.w = (a_flags & O_WRONLY) || (a_flags & O_RDWR);
                fields.a = a_flags & O_APPEND;
            }
        }ops;
        union Data {
            SharedPtr<Pipe> pipe;
            struct dirent *ep;
            Data(FileType a_type){ assert(a_type==none || a_type==stdin || a_type==stdout || a_type==stderr); }
            Data(const SharedPtr<Pipe> &a_pipe): pipe(a_pipe) {}
            Data(struct dirent *a_ep): ep(a_ep) {}
            ~Data() {}
        }obj;
        const FileType type;
        uint off;

        File(FileType a_type): type(a_type), obj(a_type) {}
        File(FileType a_type, FileOp a_ops=FileOp::none): type(a_type), obj(a_type), ops(a_ops) {}
        File(FileType a_type, int a_flags): type(a_type), obj(a_type), ops(a_flags) {}
        File(const SharedPtr<Pipe> &a_pipe, FileOp a_ops): type(FileType::pipe), obj(a_pipe), ops(a_ops) {}
        File(const SharedPtr<Pipe> &a_pipe, int a_flags): type(FileType::pipe), obj(a_pipe), ops(a_flags) {}
        File(struct dirent *a_ep, FileOp a_ops): type(FileType::entry), obj(a_ep), ops(a_ops) {}
        File(struct dirent *a_ep, int a_flags): type(FileType::entry), obj(a_ep), ops(a_flags) {}
        ~File();
        xlen_t write(xlen_t addr,size_t len);
        xlen_t read(xlen_t addr, size_t len);
    };
}
#endif