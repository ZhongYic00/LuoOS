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
    using eastl::map;

    class DEntry;

    class SuperBlock {
        public:
            SuperBlock() = default;
            SuperBlock(const SuperBlock& a_spblk) = default;
            virtual ~SuperBlock();
            virtual SuperBlock& operator=(const SuperBlock& a_spblk);
            virtual shared_ptr<DEntry> getRoot() const;  // 返回指向该超级块的根目录项的共享指针
    };
    class FileSystem {
        public:
            FileSystem() = default;
            FileSystem(const FileSystem& a_fs) = default;
            virtual ~FileSystem();
            virtual FileSystem& operator=(const FileSystem& a_fs);
            virtual string rFSType() const;  // 返回该文件系统的类型
            virtual shared_ptr<SuperBlock> getSpBlk() const;  // 返回指向该文件系统超级块的共享指针
    };
    class INode {
        public:
            INode() = default;
            INode(const INode& a_inode) = default;
            virtual ~INode();  // 需要保证脏数据落盘
            virtual INode& operator=(const INode& a_inode);
            virtual shared_ptr<INode> nodCreate(string a_name, int a_attr);  // 在该INode（必须是目录）下创建一个名为a_name、属性为a_attr的文件，返回指向该文件对应INode的共享指针
            virtual void nodRemove();  // 删除该INode对应的磁盘文件内容
            virtual int nodLink(shared_ptr<INode> a_inode);  // 软链接，返回错误码
            virtual int nodUnlink();  // 删除该软链接，返回错误码
            virtual void nodTrunc();  // 清空该INode的元信息，并标志该INode为脏
            virtual int nodRead(bool a_usrdst, uint64 a_dst, uint a_off, uint a_len);  // 从该文件的a_off偏移处开始，读取a_len字节的数据到a_dst处，返回实际读取的字节数
            virtual int nodWrite(bool a_usrsrc, uint64 a_src, uint a_off, uint a_len);  // 从a_src处开始，写入a_len字节的数据到该文件的a_off偏移处，返回实际写入的字节数
            virtual uint8 rAttr() const;  // 返回该文件的属性
            virtual uint8 rDev() const;  // 返回该文件所在文件系统的设备号
            virtual uint32 rFileSize() const;  // 返回该文件的字节数
            virtual uint32 rINo() const;  // 返回该INode的ino
            virtual shared_ptr<SuperBlock> getSpBlk() const;  // 返回指向该INode所属文件系统超级块的共享指针;
    };
    class DEntry {
        public:
            DEntry() = default;
            DEntry(const FileSystem& a_entry) = default;
            virtual ~DEntry();
            virtual DEntry& operator=(const FileSystem& a_entry);
            virtual shared_ptr<DEntry> entSearch(string a_dirname, uint *a_off);  // 在该目录项下搜索名为a_dirname的目录项，返回指向目标目录项的共享指针（找不到则返回nullptr）
            virtual int entMount(shared_ptr<DEntry> a_dev);  // 将该目录项作为挂载点，挂载以a_dev作为根目录项的设备，返回错误码
            virtual int entUnmount();  // 卸载该目录项下挂载的设备，返回错误码
            virtual const char *rName() const;  // 返回该目录项的文件名
            virtual shared_ptr<DEntry> rParent() const;  // 返回指向该目录项父目录的共享指针
            virtual shared_ptr<INode> getINode() const;  // 返回指向该目录项对应INode的共享指针
    };
    enum class FileOp:uint16_t { none=0,read=0x1,write=0x2,append=0x4, };
    union FileOps {
        struct{
            int r:1;
            int w:1;
            int a:1;
            int gc:1;
            int defer:1;
        } fields;
        FileOp raw;
        FileOps(FileOp a_ops = FileOp::none):raw(a_ops) {}
        FileOps(int a_flags) {
            fields.r = !(a_flags & O_WRONLY);
            fields.w = (a_flags & O_WRONLY) || (a_flags & O_RDWR);
            fields.a = a_flags & O_APPEND;
        }
    };
    struct File {
        enum FileType { none, pipe, entry, dev, stdin, stdout, stderr };
        FileOps ops;
        const FileType type;
        uint off=0;
        union Data {
            shared_ptr<Pipe> pipe;
            shared_ptr<DEntry> ep;
            Data(FileType a_type){ assert(a_type==none || a_type==stdin || a_type==stdout || a_type==stderr); }
            Data(const shared_ptr<Pipe> &a_pipe): pipe(a_pipe) {}
            Data(shared_ptr<DEntry> a_ep): ep(a_ep) {}
            ~Data() {}
        }obj;
        File(FileType a_type): type(a_type), obj(a_type) {}
        File(FileType a_type, FileOp a_ops = FileOp::none): type(a_type), obj(a_type), ops(a_ops) {}
        File(FileType a_type, int a_flags): type(a_type), obj(a_type), ops(a_flags) {}
        File(const shared_ptr<Pipe> &a_pipe, FileOp a_ops): type(FileType::pipe), obj(a_pipe), ops(a_ops) { if(ops.fields.r)obj.pipe->addReader(); if(ops.fields.w)obj.pipe->addWriter(); }
        File(shared_ptr<DEntry> a_ep, FileOp a_ops): type(FileType::entry), obj(a_ep), ops(a_ops) {}
        File(shared_ptr<DEntry> a_ep, int a_flags): type(FileType::entry), obj(a_ep), ops(a_flags) {}
        ~File();
        xlen_t write(xlen_t addr, size_t len);
        klib::ByteArray read(size_t len, long a_off = -1, bool a_update = true);
        klib::ByteArray readAll();
    };
    class Path {
        private:
            string pathname;
            vector<string> dirname;
        public:
            Path() = default;
            Path(const Path& a_path) = default;
            Path(const string& a_str):pathname(a_str), dirname() { pathBuild(); }
            Path(const char *a_str):pathname(a_str), dirname() { pathBuild(); }
            ~Path() = default;
            Path& operator=(const Path& a_path) = default;
            void pathBuild();
            shared_ptr<DEntry> pathSearch(shared_ptr<File> a_file, bool a_parent) const;  // @todo 返回值改为File类型
            inline shared_ptr<DEntry> pathSearch(shared_ptr<File> a_file) const { return pathSearch(a_file, false); }
            inline shared_ptr<DEntry> pathSearch(bool a_parent) const { return pathSearch(nullptr, a_parent); }
            inline shared_ptr<DEntry> pathSearch() const { return pathSearch(nullptr, false); }
            shared_ptr<DEntry> pathCreate(short a_type, int a_mode, shared_ptr<File> a_file = nullptr) const;
            int pathRemove(shared_ptr<File> a_file = nullptr) const;
            int pathLink(shared_ptr<File> a_f1, const Path& a_newpath, shared_ptr<File> a_f2) const;
            inline int pathLink(const Path& a_newpath, shared_ptr<File> a_f2) const { return pathLink(nullptr, a_newpath, a_f2); }
            inline int pathLink(shared_ptr<File> a_f1, const Path& a_newpath) const { return pathLink(a_f1, a_newpath, nullptr); }
            inline int pathLink(const Path& a_newpath) const { return pathLink(nullptr, a_newpath, nullptr); }
            int pathUnlink(shared_ptr<File> a_file = nullptr) const;
            // shared_ptr<File> pathOpen(int a_flags, shared_ptr<File> a_file) const;
            // inline shared_ptr<File> pathOpen(shared_ptr<File> a_file) const { return pathOpen(0, a_file); }
            // inline shared_ptr<File> pathOpen(int a_flags) const { return pathOpen(a_flags, nullptr); }
            // inline shared_ptr<File> pathOpen() const { return pathOpen(0, nullptr); }
    };
	class DStat {
        public:
            uint64 d_ino;	// 索引结点号
            int64 d_off;	// 到下一个dirent的偏移
            uint16 d_reclen;	// 当前dirent的长度
            uint8 d_type;	// 文件类型
            char d_name[STAT_MAX_NAME + 1];	//文件名
        // public:
            DStat() = default;
            DStat(const DStat& a_dstat):d_ino(a_dstat.d_ino), d_off(a_dstat.d_off), d_reclen(a_dstat.d_reclen), d_type(a_dstat.d_type), d_name() { strncpy(d_name, a_dstat.d_name, STAT_MAX_NAME); }
            DStat(shared_ptr<DEntry> a_entry):d_ino(a_entry->getINode()->rINo()), d_off(0), d_reclen(a_entry->getINode()->rFileSize()), d_type((a_entry->getINode()->rAttr()&ATTR_DIRECTORY) ? S_IFDIR : S_IFREG), d_name() { strncpy(d_name, a_entry->rName(), STAT_MAX_NAME); }
            ~DStat() = default;
	};
	class Stat {
        public:
            char name[STAT_MAX_NAME + 1]; // 文件名
            int dev;     // File system's disk device // 文件系统的磁盘设备
            short type;  // Type of file // 文件类型
            uint64 size; // Size of file in bytes // 文件大小(字节)
        // public:
            Stat() = default;
            Stat(const Stat& a_stat):name(), dev(a_stat.dev), type(a_stat.type), size(a_stat.size) { strncpy(name, a_stat.name, STAT_MAX_NAME); }
            Stat(shared_ptr<DEntry> a_entry):name(), dev(a_entry->getINode()->rDev()), type((a_entry->getINode()->rAttr()&ATTR_DIRECTORY) ? T_DIR : T_FILE), size(a_entry->getINode()->rFileSize()) { strncpy(name, a_entry->rName(), STAT_MAX_NAME); }
            ~Stat() = default;
	};
	class KStat {
        public:
            dev_t st_dev;  			/* ID of device containing file */
            ino_t st_ino;  			/* Inode number */
            mode_t st_mode;  		/* File type and mode */
            nlink_t st_nlink;  		/* Number of hard links */
            uid_t st_uid;			/* User ID of owner */
            gid_t st_gid;			/* Group ID of owner */
            dev_t st_rdev;			/* Device ID (if special file) */
            unsigned long __pad;	
            size_t st_size;			/* Total size, in bytes */
            blksize_t st_blksize;	/* Block size for filesystem I/O */
            int __pad2; 			
            blkcnt_t st_blocks;		/* Number of 512B blocks allocated */
            long st_atime_sec;		/* Time of last access */
            long st_atime_nsec;		
            long st_mtime_sec;		/* Time of last modification */
            long st_mtime_nsec;
            long st_ctime_sec;		/* Time of last status change */
            long st_ctime_nsec;
            // unsigned __unused[2];
            unsigned _unused[2]; // @todo 上面的写法在未实际使用的情况下过不了编译，最后要确定这个字段在我们的项目中是否有用，是否保留
        // public:
            KStat() = default;
            KStat(const KStat& a_kstat):st_dev(a_kstat.st_dev), st_ino(a_kstat.st_ino), st_mode(a_kstat.st_mode), st_nlink(a_kstat.st_nlink), st_uid(a_kstat.st_uid), st_gid(a_kstat.st_gid), st_rdev(a_kstat.st_rdev), __pad(a_kstat.__pad), st_size(a_kstat.st_size), st_blksize(a_kstat.st_blksize), __pad2(a_kstat.__pad2), st_blocks(a_kstat.st_blocks), st_atime_sec(a_kstat.st_atime_sec), st_atime_nsec(a_kstat.st_atime_nsec), st_mtime_sec(a_kstat.st_mtime_sec), st_mtime_nsec(a_kstat.st_mtime_nsec), st_ctime_sec(a_kstat.st_ctime_sec), st_ctime_nsec(a_kstat.st_ctime_nsec), _unused() { memmove(_unused, a_kstat._unused, sizeof(_unused)); }
            KStat(shared_ptr<DEntry> a_entry):st_dev(a_entry->getINode()->rDev()), st_ino(a_entry->getINode()->rINo()), st_mode((a_entry->getINode()->rAttr()&ATTR_DIRECTORY) ? S_IFDIR : S_IFREG), st_nlink(1), st_uid(0), st_gid(0), st_rdev(0), __pad(0), st_size(a_entry->getINode()->rFileSize()), st_blksize(a_entry->getINode()->getFS()->rBPC()), __pad2(0), st_blocks(st_size / st_blksize), st_atime_sec(0), st_atime_nsec(0), st_mtime_sec(0), st_mtime_nsec(0), st_ctime_sec(0), st_ctime_nsec(0), _unused({ 0, 0 }) { if(st_blocks*st_blksize < st_size) { ++st_blocks; } }
            ~KStat() = default;

	};

    extern map<uint8, shared_ptr<FileSystem>> dev_table;
    // namespace internal{
    //     class SuperBlock {};
    //     /// @bug sharedptr会直接删除，需要引入cache
    //     class INode;
    //     class DEntry;
    //     typedef shared_ptr<INode> INodeRef;
    //     class FileSystem{
    //     public:
    //         FileSystem(){}
    //         virtual ~FileSystem(){}
    //         virtual shared_ptr<INode> mknod()=0;
    //         virtual INodeRef getRoot()=0;
    //         // no rmnod since inode is managed by its refcount?
    //     };
    //     class INode{
    //     private:
    //         struct hlist_node i_hash;
    //         struct list_head i_list;
    //         struct list_head i_sb_list;
    //         struct list_head i_dentry;
    //         uint64 i_ino;
    //         int i_count;  // @todo: 原类型为atomic_t，寻找替代？
    //         uint i_nlink;
    //         uid_t i_uid;  //inode拥有者id
    //         gid_t i_gid;  //inode所属群组id
    //         dev_t i_rdev;  //若是设备文件，表示记录设备的设备号
    //         u64 i_version;
    //         uint32 i_size;  //文件所占字节数(原loff_t类)
    //         struct timespec i_atime;  //inode最近一次的存取时间
    //         struct timespec i_mtime;  //inode最近一次修改时间
    //         struct timespec i_ctime;  //inode的生成时间
    //         uint  i_blkbits;
    //         blkcnt_t  i_blocks;  // 文件所占扇区数
    //         uint16 i_bytes;  // inode本身的字节数
    //         mode_t i_mode;  // 文件权限
    //         SuperBlock *i_sb;
    //         struct address_space *i_mapping;
    //         struct address_space i_data;
    //         struct list_head i_devices;
    //         union {
    //             struct pipe_inode_info *i_pipe;
    //             struct block_device *i_bdev;
    //             struct cdev  *i_cdev;  //若是字符设备，对应的为cdev结构
    //         };
    //         ///@todo other metadata
    //     public:
    //         virtual DEntry *lookUp(INode *a_inode, DEntry *a_dent, string a_name);
    //         virtual ~INode()=default;
    //         virtual expected<klib::ByteArray,Err> read(size_t off,size_t len)=0;
    //         virtual expected<xlen_t,Err> write(size_t off,klib::ByteArray bytes)=0;
    //     };
    //     class Directory:public INode{
    //     public:
    //         /// @brief lookup
    //         virtual expected<INodeRef,Err> find(const string& name)=0;
    //         virtual expected<void,Err> link(const string& name,INodeRef inode)=0;
    //         virtual expected<void,Err> unlink(const DEntry &sub)=0;
    //     };
    //     class DEntry{
    //     public:
    //         shared_ptr<INode> nod;
    //         const string name;
    //         const shared_ptr<DEntry> parent;
    //     };
    //     /// @brief a cursor in vfs
    //     class Dir{
    //         DEntry cur;
    //     public:
    //         bool mkdir(const string& name){ auto sub=cur.nod->fs->mknod(); eastl::dynamic_pointer_cast<Directory>(cur.nod)->link(name,sub); }
    //         bool rmdir(const string& name){}
    //         bool cd(const string& name){ if(auto rt=eastl::dynamic_pointer_cast<Directory>(cur.nod)->find(name)){ cur=rt.value(); } }
    //         bool cdUp(){}
    //     };
    //     struct FileSystem_:private FileSystem{ FileSystem_():FileSystem(){} };
    // }
}
#endif