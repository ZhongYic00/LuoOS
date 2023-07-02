#ifndef FS_HH__
#define FS_HH__

#include "ipc.hh"
#include "resmgr.hh"
#include "linux/fcntl.h"
#include "error.hh"
#include "EASTL/string.h"

namespace fs{
    using pipe::Pipe;
    using eastl::vector;
    using eastl::string;
    using eastl::shared_ptr;
    using eastl::make_shared;

    class SuperBlock;
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
        /// @bug sharedptr会直接删除，需要引入cache
        class Inode;
        class Dentry;
        typedef shared_ptr<Inode> InodeRef;
        class FileSystem{
        public:
            FileSystem(){}
            virtual ~FileSystem(){}
            virtual shared_ptr<Inode> mknod()=0;
            virtual InodeRef getRoot()=0;
            // no rmnod since inode is managed by its refcount?
        };
        class Inode{
        public:
            const shared_ptr<FileSystem> fs;
            ino_t ino;    // from types.h, may be too small?
            FileOps ops;
            uid_t uid;
            gid_t gid;
            size_t size;
            ///@todo other metadata

            Inode(ino_t ino):ino(ino){}
            virtual ~Inode()=default;
            virtual expected<klib::ByteArray,Err> read(size_t off,size_t len)=0;
            virtual expected<xlen_t,Err> write(size_t off,klib::ByteArray bytes)=0;
        };
        class Directory:public Inode{
        public:
            /// @brief lookup
            virtual expected<InodeRef,Err> find(const string& name)=0;
            virtual expected<void,Err> link(const string& name,InodeRef inode)=0;
            virtual expected<void,Err> unlink(const Dentry &sub)=0;
        };
        class Dentry{
        public:
            shared_ptr<Inode> nod;
            const string name;
            const shared_ptr<Dentry> parent;
        };
    }
    /// @brief a cursor in vfs
    class Dir{
        internal::Dentry cur;
    public:
        bool mkdir(const string& name){
            auto sub=cur.nod->fs->mknod();
            eastl::dynamic_pointer_cast<internal::Directory>(cur.nod)->link(name,sub);
        }
        bool rmdir(const string& name){}
        bool cd(const string& name){
            if(auto rt=eastl::dynamic_pointer_cast<internal::Directory>(cur.nod)->find(name)){
                // cur=rt.value();
            }
        }
        bool cdUp(){}
    };
    struct FileSystem_:private internal::FileSystem{
        FileSystem_():internal::FileSystem(){}
    };

    struct DirEnt;

    struct File {
        enum FileType { none,pipe,entry,dev, stdin,stdout,stderr };
        FileOps ops;
        union Data {
            shared_ptr<Pipe> pipe;
            DirEnt *ep;
            Data(FileType a_type){ assert(a_type==none || a_type==stdin || a_type==stdout || a_type==stderr); }
            Data(const shared_ptr<Pipe> &a_pipe): pipe(a_pipe) {}
            Data(DirEnt *a_ep): ep(a_ep) {}
            ~Data() {}
        }obj;
        const FileType type;
        uint off=0;

        File(FileType a_type): type(a_type), obj(a_type) {}
        File(FileType a_type, FileOp a_ops=FileOp::none): type(a_type), obj(a_type), ops(a_ops) {}
        File(FileType a_type, int a_flags): type(a_type), obj(a_type), ops(a_flags) {}
        File(const shared_ptr<Pipe> &a_pipe, FileOp a_ops): type(FileType::pipe), obj(a_pipe), ops(a_ops) {
            if(ops.fields.r)obj.pipe->addReader();
            if(ops.fields.w)obj.pipe->addWriter();
        }
        File(DirEnt *a_ep, FileOp a_ops): type(FileType::entry), obj(a_ep), ops(a_ops) {}
        File(DirEnt *a_ep, int a_flags): type(FileType::entry), obj(a_ep), ops(a_flags) {}
        ~File();
        xlen_t write(xlen_t addr,size_t len);
        klib::ByteArray read(size_t len, long a_off=-1, bool a_update=true);
        klib::ByteArray readAll();
    };
}
#endif