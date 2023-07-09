#ifndef FAT_HH__
#define FAT_HH__

#include "fs.hh"
#include "bio.hh"

namespace fs {
    using BlockBuf = struct bio::BlockBuf;
    class SuperBlock;
    typedef struct ShortNameEntry_t {
        char name[CHAR_SHORT_NAME];  // 文件名.扩展名（8+3）
        uint8 attr; // 属性
        uint8 _nt_res; // 保留
        uint8 _crt_time_tenth; // 创建时间的10ms位
        uint16 _crt_time; // 文件创建时间
        uint16 _crt_date; // 文件创建日期
        uint16 _lst_acce_date; // 文件最后访问日期
        uint16 fst_clus_hi;  // 文件起始簇号的高16位
        uint16 _lst_wrt_time;  // 文件最近修改的时间
        uint16 _lst_wrt_date; // 文件最近修改的日期
        uint16 fst_clus_lo;  // 文件起始簇号的低16位
        uint32 file_size;  // 文件的大小
        void readEntName(char *a_buf) const;
    } __attribute__((packed, aligned(4))) SNE;
    typedef struct LongNameEntry_t {
        uint8 order; // 目录项序列号
        wchar name1[5]; // 第1~5个字符的unicode码
        uint8 attr; // 长目录项的属性标志，一定是0x0F。
        uint8 _type; // 系统保留
        uint8 checksum; // 校验和
        wchar name2[6]; // 第6~11个字符的unicode码
        uint16 _fst_clus_lo; // 文件起始簇号 保留 目前常置0
        wchar name3[2]; // 第12~13个字符的unicode码
        void readEntName(char *a_buf) const;
    } __attribute__((packed, aligned(4))) LNE;
    union Ent {
        SNE sne; // short name entry
        LNE lne; // long name entry
        void readEntName(char *a_buf) const;
    };
    class DirEnt {
        public:
            char  filename[FAT32_MAX_FILENAME + 1];  // 文件名
            uint8   attribute;  // 属性
            uint32  first_clus;  // 起始簇号
            uint32  file_size;  // 文件大小
            uint32  cur_clus;  // 当前簇号
            uint clus_cnt;  // 当前簇是该文件的第几个簇
            uint8 dev;   // 设备号
            bool dirty;  // 浊/清
            short valid;  // 合法性
            int ref; // 关联数
            uint32 off; // 父目录的entry的偏移 // offset in the parent dir entry, for writing convenience
            DirEnt *parent; // because FAT32 doesn't have such thing like inum, use this for cache trick
            DirEnt *next; // 
            DirEnt *prev; // 
            bool mount_flag;
        // public:
            // @todo 暂时不能写构造函数，会禁用初始化列表
            DirEnt() = default;
            DirEnt(const DirEnt& a_entry):filename(), attribute(a_entry.attribute), first_clus(a_entry.first_clus), file_size(a_entry.file_size), cur_clus(a_entry.cur_clus), clus_cnt(a_entry.clus_cnt), dev(a_entry.dev), dirty(a_entry.dirty), valid(a_entry.valid), ref(a_entry.ref), off(a_entry.off), parent(a_entry.parent), next(a_entry.next), prev(a_entry.prev), mount_flag(a_entry.mount_flag) { strncpy(filename, a_entry.filename, FAT32_MAX_FILENAME); }
            DirEnt(const char *a_name, uint8 a_attr, uint32 a_first_clus, uint8 a_dev, bool a_mount_flag):filename(), attribute(a_attr), first_clus(a_first_clus), file_size(0), cur_clus(first_clus), clus_cnt(0), dev(a_dev), dirty(false), valid(1), ref(0), off(0), parent(nullptr), next(this), prev(this), mount_flag(a_mount_flag) { strncpy(filename, a_name, FAT32_MAX_FILENAME); }
            ~DirEnt() = default;
            DirEnt& operator=(const union Ent& a_ent);
            DirEnt *entSearch(string a_dirname, uint *a_off = nullptr);
            int entNext(DirEnt *const a_entry, uint a_off, int *const a_count);
            inline int entNext(DirEnt *const a_entry, uint a_off) { return entNext(a_entry, a_off, nullptr); }
            inline int entNext(uint a_off, int *const a_count) { return entNext(nullptr, a_off, a_count); }
            inline int entNext(uint a_off) { return entNext(nullptr, a_off, nullptr); }
            int relocClus(uint a_off, bool a_alloc);
            const uint32 allocClus() const;
            inline DirEnt *entDup();
            DirEnt *eCacheHit(string a_name) const;
            void entRelse();
            void entTrunc();
            void parentUpdate();
            DirEnt *entCreate(string a_name, int a_attr);
            void entCreateOnDisk(const DirEnt *a_entry, uint a_off);
            int entRead(bool a_usrdst, uint64 a_dst, uint a_off, uint a_len);
            int entWrite(bool a_usrsrc, uint64 a_src, uint a_off, uint a_len);
            void entRemove();
            inline bool isEmpty() { return entNext(2 * 32) == -1; }  // skip the "." and ".."
            int entMount(const DirEnt *a_dev);
            int entUnmount();
            int entLink(DirEnt *a_entry) const;
            int entUnlink() const;
    };
    struct Link{
        union Ent de;
        uint32 link_count;
    };
    class SuperBlock {
        private:
            uint32 first_data_sec; // data所在的第一个扇区
            uint32 data_sec_cnt; // 数据扇区数
            uint32 data_clus_cnt; // 数据簇数
            uint32 byts_per_clus; // 每簇字节数
        protected:
            struct BPB_t {
                uint16 byts_per_sec;  // 扇区字节数
                uint8 sec_per_clus;  // 每簇扇区数
                uint16 rsvd_sec_cnt;  // 保留扇区数
                uint8 fat_cnt;  // fat数
                uint32 hidd_sec;  // 隐藏扇区数
                uint32 tot_sec;  // 总扇区数
                uint32 fat_sz;   // 一个fat所占扇区数
                uint32 root_clus; // 根目录簇号
                BPB_t() = default;
                BPB_t(const BPB_t& a_bpb) = default;
                BPB_t(const BlockBuf &a_blk):byts_per_sec(a_blk.at<uint16_t>(11)), sec_per_clus(a_blk.at<uint8>(13)), rsvd_sec_cnt(a_blk.at<uint16>(14)), fat_cnt(a_blk.at<uint8>(16)), hidd_sec(a_blk.at<uint32>(28)), tot_sec(a_blk.at<uint32>(32)), fat_sz(a_blk.at<uint32>(36)), root_clus(a_blk.at<uint32>(44)) {}
                BPB_t(uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):byts_per_sec(a_bps), sec_per_clus(a_spc), rsvd_sec_cnt(a_rsc), fat_cnt(a_fc), hidd_sec(a_hs), tot_sec(a_ts), fat_sz(a_fs), root_clus(a_rc) {}
                ~BPB_t() = default;
                BPB_t& operator=(const BPB_t& a_bpb) = default;
            } bpb;
            typedef BPB_t BPB;
        public:
            SuperBlock() = default;
            SuperBlock(const SuperBlock& a_spblk) = default;
            SuperBlock(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, BPB a_bpb):first_data_sec(a_fds), data_sec_cnt(a_dsc), data_clus_cnt(a_dcc), byts_per_clus(a_bpc), bpb(a_bpb) {}
            SuperBlock(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):first_data_sec(a_fds), data_sec_cnt(a_dsc), data_clus_cnt(a_dcc), byts_per_clus(a_bpc), bpb(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc) {}
            SuperBlock(const BPB& a_bpb):first_data_sec(a_bpb.rsvd_sec_cnt+a_bpb.fat_cnt*a_bpb.fat_sz), data_sec_cnt(a_bpb.tot_sec-first_data_sec), data_clus_cnt(data_sec_cnt/a_bpb.sec_per_clus), byts_per_clus(a_bpb.sec_per_clus*a_bpb.byts_per_sec), bpb(a_bpb) {}
            SuperBlock(const BlockBuf &a_blk):SuperBlock(BPB(a_blk)) {}
            SuperBlock(uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):SuperBlock(BPB(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc)) {}
            ~SuperBlock() = default;
            SuperBlock& operator=(const SuperBlock& a_spblk) = default;
            inline const uint32 rFDS() const { return first_data_sec; }
            inline const uint32 rDSC() const { return data_sec_cnt; }
            inline const uint32 rDCC() const { return data_clus_cnt; }
            inline const uint32 rBPC() const { return byts_per_clus; }
            inline const BPB rBPB() const { return bpb; }
            inline const uint16 rBPS() const { return bpb.byts_per_sec; }
            inline const uint8 rSPC() const { return bpb.sec_per_clus; }
            inline const uint16 rRSC() const { return bpb.rsvd_sec_cnt; }
            inline const uint8 rFC() const { return bpb.fat_cnt; }
            inline const uint32 rHS() const { return bpb.hidd_sec; }
            inline const uint32 rTS() const { return bpb.tot_sec; }
            inline const uint32 rFS() const { return bpb.fat_sz; }
            inline const uint32 rRC() const { return bpb.root_clus; }
            const uint rwClus(uint32 a_cluster, bool a_iswrite, bool a_usrbuf, uint64 a_buf, uint a_off, uint a_len) const;
            inline const uint32 firstSec(uint32 a_cluster) const { return (a_cluster-2)*rSPC() + rFDS(); }
            const uint32 fatRead(uint32 a_cluster) const;
            inline const uint32 numthSec(uint32 a_cluster, uint8 a_fat_num) const { return rRSC() + (a_cluster<<2) / rBPS() + rFS() * (a_fat_num-1); }
            inline const uint32 secOffset(uint32 a_cluster) const { return (a_cluster<<2) % rBPS(); }
            void clearClus(uint32 a_cluster) const;
            const int fatWrite(uint32 a_cluster, uint32 a_content) const;
            inline void freeClus(uint32 a_cluster) const { fatWrite(a_cluster, 0); }
    };
    class FileSystem:public SuperBlock {
        private:
            bool valid;
            shared_ptr<DEntry> root;  //这个文件系统的根目录文件
            bool mount_mode;  //是否被挂载的标志
        public:
            FileSystem() = default;
            FileSystem(const FileSystem& a_fs) = default;
            FileSystem(const SuperBlock& a_spblk, bool a_valid, shared_ptr<DEntry> a_root, bool a_mm):SuperBlock(a_spblk), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, BPB a_bpb, bool a_valid, shared_ptr<DEntry> a_root, bool a_mm):SuperBlock(a_fds, a_dsc, a_dcc, a_bpc, a_bpb), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc, bool a_valid, shared_ptr<DEntry> a_root, bool a_mm):SuperBlock(a_fds, a_dsc, a_dcc, a_bpc, a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(const BPB& a_bpb, bool a_valid, shared_ptr<DEntry> a_root, bool a_mm):SuperBlock(a_bpb), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(const BlockBuf& a_blk, bool a_valid, shared_ptr<DEntry> a_root, bool a_mm):SuperBlock(a_blk), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc, bool a_valid, shared_ptr<DEntry> a_root, bool a_mm):SuperBlock(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            ~FileSystem() = default;
            FileSystem& operator=(const FileSystem& a_fs) = default;
            inline bool isValid() const { return valid; }
            inline shared_ptr<DEntry> getRoot() const { return &root; }
            inline bool isMounted() const { return mount_mode; }
    };
    extern FileSystem dev_fat[8];  // @todo 移到内核对象中
    class INode {
        private:
            uint32 inode_num;
            SuperBlock *spblk;
            DirEnt *entry;
            inline void nodRelse() const { if(entry != nullptr) { entry->entRelse(); } }
            inline DirEnt *nodDup() const { return entry==nullptr ? nullptr : entry->entDup(); }
            inline DirEnt *nodDup(DirEnt *a_entry) const { return a_entry==nullptr ? nullptr : a_entry->entDup(); }
            inline void nodPanic() const { if(entry == nullptr) { panic("INode panic!\n"); } }
        public:
            INode():inode_num(0), spblk(nullptr), entry(nullptr) {}
            INode(const INode& a_inode) = default;
            INode(DirEnt *a_entry):inode_num(a_entry->first_clus), spblk(&(dev_fat[a_entry->dev])), entry(a_entry) {}
            ~INode() { nodRelse(); }
            inline INode& operator=(const INode& a_inode) { nodRelse(); inode_num = a_inode.inode_num; spblk = a_inode.spblk; entry = a_inode.nodDup(); return *this; }
            inline INode& operator=(DirEnt *a_entry) { nodRelse(); inode_num = a_entry->first_clus; spblk = &(dev_fat[a_entry->dev]); entry = nodDup(a_entry); return *this; }
            inline shared_ptr<INode> nodCreate(string a_name, int a_attr) const { nodPanic(); DirEnt *ret = entry->entCreate(a_name, a_attr); return ret==nullptr ? nullptr : make_shared<INode>(ret); }
            inline void nodRemove() const { nodPanic(); entry->entRemove(); }
            inline int nodLink(shared_ptr<INode> a_inode) const { nodPanic(); return entry->entLink(a_inode->entry); }
            inline int nodUnlink() const { nodPanic(); return entry->entUnlink(); }
            inline void nodTrunc() const { nodPanic(); entry->entTrunc(); }
            inline int nodRead(bool a_usrdst, uint64 a_dst, uint a_off, uint a_len) const { nodPanic(); return entry->entRead(a_usrdst, a_dst, a_off, a_len); }
            inline int nodWrite(bool a_usrsrc, uint64 a_src, uint a_off, uint a_len) const { nodPanic(); return entry->entWrite(a_usrsrc, a_src, a_off, a_len); }
            inline uint8 rAttr() const { nodPanic(); return entry->attribute; }
            inline uint8 rDev() const { nodPanic(); return entry->dev; }
            inline uint32 rFileSize() const { nodPanic(); return entry->file_size; }
            inline uint32 rINo() const { nodPanic(); return inode_num; }
            inline SuperBlock *getFS() const { nodPanic(); return spblk; }
            inline DirEnt *rawPtr() const { return nodDup(); }
    };
    class DEntry {
        private:
            shared_ptr<INode> inode;
            DirEnt *entry;
            inline void dERelse() const { if(entry != nullptr) { entry->entRelse(); } }
            inline void dEPanic() const { if(entry == nullptr) { panic("DEntry panic!\n"); } }
        public:
            DEntry():inode(nullptr), entry(nullptr) {}
            DEntry(const DEntry& a_dentry):inode(a_dentry.inode), entry(a_dentry.inode->rawPtr()) {}
            DEntry(shared_ptr<INode> a_inode):inode(a_inode), entry(a_inode->rawPtr()) {}
            DEntry(DirEnt *a_entry):inode(make_shared<INode>(a_entry)), entry(inode->rawPtr()) {}
            ~DEntry() { dERelse(); }
            inline DEntry& operator=(const DEntry& a_dentry) { dERelse(); inode = a_dentry.inode; entry = a_dentry.inode->rawPtr(); return *this; }
            inline DEntry& operator=(shared_ptr<INode> a_inode) { dERelse(); inode = a_inode; entry = a_inode->rawPtr(); return *this; }
            inline DEntry& operator=(DirEnt *a_entry) { dERelse(); inode = make_shared<INode>(a_entry), entry = inode->rawPtr(); return *this; }
            inline shared_ptr<DEntry> entSearch(string a_dirname, uint *a_off = nullptr) const { dEPanic(); DirEnt *ret = entry->entSearch(a_dirname, a_off); return ret==nullptr ? nullptr : make_shared<DEntry>(ret); }
            inline bool isEmpty() const { dEPanic(); return entry->isEmpty(); }
            inline const char *rName() const { dEPanic(); return entry->filename; }
            inline shared_ptr<DEntry> rParent() const { dEPanic(); return entry->parent==nullptr ? nullptr : make_shared<DEntry>(entry->parent); }
            inline bool isMntPoint() const { dEPanic(); return entry->mount_flag; }
            inline shared_ptr<INode> getINode() const { return inode; }
            inline DirEnt *rawPtr() const { return inode==nullptr ? nullptr : inode->rawPtr(); }
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
    int rootFSInit();
}

#endif