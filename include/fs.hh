#ifndef FS_HH__
#define FS_HH__

#include "ipc.hh"
#include "resmgr.hh"
#include "linux/fcntl.h"
#include "error.hh"

namespace fs{
    using klib::SharedPtr;
    using pipe::Pipe;

    class SuperBlock{};
    class Dentry{};
    class FileSystem_{};
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
    };
    namespace internal{
        class Inode{
            const SharedPtr<FileSystem_> fs;
        public:
            const ino_t ino;    // from types.h, may be too small?
            const FileOps ops;
            const uid_t uid;
            const gid_t gid;
            size_t size;
            ///@todo other metadata
            virtual expected<klib::ByteArray,Err> read(size_t len);
            virtual expected<xlen_t,Err> write();
        };
    }

    struct DirEnt;

    struct File {
        enum FileType { none,pipe,entry,dev, stdin,stdout,stderr };
        FileOps ops;
        union Data {
            SharedPtr<Pipe> pipe;
            struct DirEnt *ep;
            Data(FileType a_type){ assert(a_type==none || a_type==stdin || a_type==stdout || a_type==stderr); }
            Data(const SharedPtr<Pipe> &a_pipe): pipe(a_pipe) {}
            Data(struct DirEnt *a_ep): ep(a_ep) {}
            ~Data() {}
        }obj;
        const FileType type;
        uint off=0;

        File(FileType a_type): type(a_type), obj(a_type) {}
        File(FileType a_type, FileOp a_ops=FileOp::none): type(a_type), obj(a_type), ops(a_ops) {}
        File(FileType a_type, int a_flags): type(a_type), obj(a_type), ops(a_flags) {}
        File(const SharedPtr<Pipe> &a_pipe, FileOp a_ops): type(FileType::pipe), obj(a_pipe), ops(a_ops) {
            if(ops.fields.r)obj.pipe->addReader();
            if(ops.fields.w)obj.pipe->addWriter();
        }
        File(struct DirEnt *a_ep, FileOp a_ops): type(FileType::entry), obj(a_ep), ops(a_ops) {}
        File(struct DirEnt *a_ep, int a_flags): type(FileType::entry), obj(a_ep), ops(a_flags) {}
        ~File();
        xlen_t write(xlen_t addr,size_t len);
        klib::ByteArray read(size_t len, long a_off=-1, bool a_update=true);
        klib::ByteArray readAll();
    };
}
#endif