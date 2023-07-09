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
        class SuperBlock {};
        /// @bug sharedptr会直接删除，需要引入cache
        class INode;
        class DEntry;
        typedef shared_ptr<INode> INodeRef;
        class FileSystem{
        public:
            FileSystem(){}
            virtual ~FileSystem(){}
            virtual shared_ptr<INode> mknod()=0;
            virtual INodeRef getRoot()=0;
            // no rmnod since inode is managed by its refcount?
        };
        class INode{
        private:
            // struct hlist_node i_hash;
            // struct list_head i_list;
            // struct list_head i_sb_list;
            // struct list_head i_dentry;
            // uint64 i_ino;
            // int i_count;  // @todo: 原类型为atomic_t，寻找替代？
            // uint i_nlink;
            // uid_t i_uid;  //inode拥有者id
            // gid_t i_gid;  //inode所属群组id
            // dev_t i_rdev;  //若是设备文件，表示记录设备的设备号
            // u64 i_version;
            // uint32 i_size;  //文件所占字节数(原loff_t类)
            // struct timespec i_atime;  //inode最近一次的存取时间
            // struct timespec i_mtime;  //inode最近一次修改时间
            // struct timespec i_ctime;  //inode的生成时间
            // uint  i_blkbits;
            // blkcnt_t  i_blocks;  // 文件所占扇区数
            // uint16 i_bytes;  // inode本身的字节数
            // mode_t i_mode;  // 文件权限
            // SuperBlock *i_sb;
            // struct address_space *i_mapping;
            // struct address_space i_data;
            // struct list_head i_devices;
            // union {
            //     struct pipe_inode_info *i_pipe;
            //     struct block_device *i_bdev;
            //     struct cdev  *i_cdev;  //若是字符设备，对应的为cdev结构
            // };
            ///@todo other metadata
        public:
            virtual DEntry *lookUp(INode *a_inode, DEntry *a_dent, string a_name);
            
            // virtual ~INode()=default;
            // virtual expected<klib::ByteArray,Err> read(size_t off,size_t len)=0;
            // virtual expected<xlen_t,Err> write(size_t off,klib::ByteArray bytes)=0;
        };
        class Directory:public INode{
        public:
            /// @brief lookup
            virtual expected<INodeRef,Err> find(const string& name)=0;
            virtual expected<void,Err> link(const string& name,INodeRef inode)=0;
            virtual expected<void,Err> unlink(const DEntry &sub)=0;
        };
        class DEntry{
        public:
            shared_ptr<INode> nod;
            const string name;
            const shared_ptr<DEntry> parent;
        };
        /// @brief a cursor in vfs
        class Dir{
            DEntry cur;
        public:
            bool mkdir(const string& name){
                // auto sub=cur.nod->fs->mknod();
                // eastl::dynamic_pointer_cast<Directory>(cur.nod)->link(name,sub);
            }
            bool rmdir(const string& name){}
            bool cd(const string& name){
                // if(auto rt=eastl::dynamic_pointer_cast<Directory>(cur.nod)->find(name)){
                //     // cur=rt.value();
                // }
            }
            bool cdUp(){}
        };
        struct FileSystem_:private FileSystem{
            FileSystem_():FileSystem(){}
        };
    }

    // class DirEnt;
    class DEntry;

    struct File {
        enum FileType { none,pipe,entry,dev, stdin,stdout,stderr };
        FileOps ops;
        union Data {
            shared_ptr<Pipe> pipe;
            shared_ptr<DEntry> ep;
            Data(FileType a_type){ assert(a_type==none || a_type==stdin || a_type==stdout || a_type==stderr); }
            Data(const shared_ptr<Pipe> &a_pipe): pipe(a_pipe) {}
            Data(shared_ptr<DEntry> a_ep): ep(a_ep) {}
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
        File(shared_ptr<DEntry> a_ep, FileOp a_ops): type(FileType::entry), obj(a_ep), ops(a_ops) {}
        File(shared_ptr<DEntry> a_ep, int a_flags): type(FileType::entry), obj(a_ep), ops(a_flags) {}
        ~File();
        xlen_t write(xlen_t addr,size_t len);
        klib::ByteArray read(size_t len, long a_off=-1, bool a_update=true);
        klib::ByteArray readAll();
    };
}
#endif