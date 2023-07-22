#ifndef FS_HH__
#define FS_HH__

#include "ipc.hh"
#include "resmgr.hh"
#include "linux/fcntl.h"
#include "error.hh"
#include "vm.hh"
#include <EASTL/map.h>

namespace fs{
    using pipe::Pipe;
    using eastl::vector;
    using eastl::string;
    using eastl::shared_ptr;
    using eastl::make_shared;
    using klib::Segment;
    using memvec=eastl::vector<Segment<xlen_t>>;

    class DEntry;
    class FileSystem;

    class SuperBlock {
        public:
            SuperBlock() = default;
            SuperBlock(const SuperBlock& a_spblk) = default;
            virtual ~SuperBlock() = default;
            SuperBlock& operator=(const SuperBlock& a_spblk);
            virtual uint32 rBlkSize() const = 0;  // 返回块大小
            virtual shared_ptr<DEntry> getRoot() const = 0;  // 返回指向该超级块的根目录项的共享指针
            virtual shared_ptr<DEntry> getMntPoint() const = 0;  // 返回文件系统在父文件系统上的挂载点，如果是根文件系统则返回根目录
            virtual FileSystem *getFS() const = 0;  // 返回指向该文件系统对象的共享指针
            virtual bool isValid() const = 0;  // 返回该超级块是否有效（Unmount后失效）
    };
    class FileSystem {
        public:
            FileSystem() = default;
            FileSystem(const FileSystem& a_fs) = default;
            virtual ~FileSystem() = default;
            FileSystem& operator=(const FileSystem& a_fs) = default;
            virtual string rFSType() const = 0;  // 返回该文件系统的类型
            virtual uint8 rKey() const = 0;  // 返回文件系统在文件系统表中的键值（不同键值的文件系统可以对应同一物理设备/设备号）
            virtual bool isRootFS() const = 0;  // 返回该文件系统是否为根文件系统
            virtual shared_ptr<SuperBlock> getSpBlk() const = 0;  // 返回指向该文件系统超级块的共享指针
            virtual int ldSpBlk(uint8 a_dev, shared_ptr<fs::DEntry> a_mnt) = 0;  // 从设备号为a_dev的物理设备上装载该文件系统的超级块，并设挂载点为a_mnt，若a_mnt为nullptr则作为根文件系统装载，返回错误码
            virtual void unInstall() = 0;  // 卸载该文件系统的超级块
    };
    class INode {
        public:
            eastl::weak_ptr<vm::VMO> vmo;
            INode() = default;
            INode(const INode& a_inode) = default;
            virtual ~INode() = default;  // 需要保证脏数据落盘
            INode& operator=(const INode& a_inode) = default;
            virtual void nodRemove() = 0;  // 删除该INode对应的磁盘文件内容
            // virtual int nodHardLink(shared_ptr<INode> a_inode);  // 硬链接，返回错误码，由pathHardLink识别各文件系统进行单独调用，不作为统一接口（参数类型不同）
            virtual int nodHardUnlink() = 0;  // 删除硬链接，返回错误码
            // virtual int nodSoftLink(shared_ptr<INode> a_inode);  // @todo: 软链接，返回错误码
            // virtual int nodSoftUnlink();  // @todo: 删除软链接，返回错误码
            virtual void nodTrunc() = 0;  // 清空该INode的元信息，并标志该INode为脏
            virtual int nodRead(bool a_usrdst, uint64 a_dst, uint a_off, uint a_len) = 0;  // 从该文件的a_off偏移处开始，读取a_len字节的数据到a_dst处，返回实际读取的字节数
            virtual int nodWrite(bool a_usrsrc, uint64 a_src, uint a_off, uint a_len) = 0;  // 从a_src处开始，写入a_len字节的数据到该文件的a_off偏移处，返回实际写入的字节数
            /// @brief copy contents from src to dst
            /// @param src memvec(lba in bytes)
            /// @param dst memvec(paddr in bytes)
            void readv(const memvec &src,const memvec &dst);
            /// @brief copy contents from src to dst
            /// @param src memvec(lba in pages)
            /// @param dst memvec(ppn)
            void readPages(const memvec &src,const memvec &dst);
            virtual uint8 rAttr() const = 0;  // 返回该文件的属性
            virtual uint8 rDev() const = 0;  // 返回该文件所在文件系统的设备号
            virtual uint32 rFileSize() const = 0;  // 返回该文件的字节数
            virtual uint32 rINo() const = 0;  // 返回该INode的ino
            virtual shared_ptr<SuperBlock> getSpBlk() const = 0;  // 返回指向该INode所属文件系统超级块的共享指针;
    };
    class DEntry {
        public:
            DEntry() = default;
            DEntry(const DEntry& a_entry) = default;
            virtual ~DEntry() = default;
            DEntry& operator=(const DEntry& a_entry) = default;
            virtual shared_ptr<DEntry> entSearch(string a_dirname, uint *a_off = nullptr) = 0;  // 在该目录项下(不包含子目录)搜索名为a_dirname的目录项，返回指向目标目录项的共享指针（找不到则返回nullptr）
            virtual shared_ptr<DEntry> entCreate(string a_name, int a_attr) = 0;  // 在该目录项下以a_attr属性创建名为a_name的文件，返回指向该文件目录项的共享指针
            virtual void setMntPoint(const FileSystem *a_fs) = 0;  // 设该目录项为a_fs文件系统的挂载点（不更新a_fs）
            virtual void clearMnt() = 0;  // 清除该目录项的挂载点记录
            virtual const char *rName() const = 0;  // 返回该目录项的文件名
            virtual shared_ptr<DEntry> getParent() const = 0;  // 返回指向该目录项父目录的共享指针
            virtual shared_ptr<INode> getINode() const = 0;  // 返回指向该目录项对应INode的共享指针
            virtual bool isMntPoint() const = 0;  // 返回该目录项是否为一个挂载点
            virtual bool isEmpty() const = 0;  // 返回该目录项是否为空
            virtual bool isRoot() const = 0;  // 返回该目录项是否为其所在文件系统的根目录
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
        shared_ptr<vm::VMO> vmo();
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
            int pathHardLink(shared_ptr<File> a_f1, const Path& a_newpath, shared_ptr<File> a_f2) const;
            inline int pathHardLink(const Path& a_newpath, shared_ptr<File> a_f2) const { return pathHardLink(nullptr, a_newpath, a_f2); }
            inline int pathHardLink(shared_ptr<File> a_f1, const Path& a_newpath) const { return pathHardLink(a_f1, a_newpath, nullptr); }
            inline int pathHardLink(const Path& a_newpath) const { return pathHardLink(nullptr, a_newpath, nullptr); }
            int pathHardUnlink(shared_ptr<File> a_file = nullptr) const;
            int pathMount(const Path& a_devpath, string a_fstype) const;
            int pathUnmount() const;
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
            KStat(shared_ptr<DEntry> a_entry):st_dev(a_entry->getINode()->rDev()), st_ino(a_entry->getINode()->rINo()), st_mode((a_entry->getINode()->rAttr()&ATTR_DIRECTORY) ? S_IFDIR : S_IFREG), st_nlink(1), st_uid(0), st_gid(0), st_rdev(0), __pad(0), st_size(a_entry->getINode()->rFileSize()), st_blksize(a_entry->getINode()->getSpBlk()->rBlkSize()), __pad2(0), st_blocks(st_size / st_blksize), st_atime_sec(0), st_atime_nsec(0), st_mtime_sec(0), st_mtime_nsec(0), st_ctime_sec(0), st_ctime_nsec(0), _unused({ 0, 0 }) { if(st_blocks*st_blksize < st_size) { ++st_blocks; } }
            ~KStat() = default;

	};
    int rootFSInit();
}
#endif