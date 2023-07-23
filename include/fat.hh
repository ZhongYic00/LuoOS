#ifndef FAT_HH__
#define FAT_HH__

#include "fs.hh"
#include "bio.hh"

namespace fat {
    using BlockBuf = struct bio::BlockBuf;
    using fs::File;
    using eastl::string;
    using eastl::shared_ptr;
    using eastl::make_shared;
    class FileSystem;
    class DirEnt;
    class DEntry;

    class SuperBlock:public fs::SuperBlock {
        private:
            shared_ptr<DEntry> root;  //根目录
            shared_ptr<fs::DEntry> mnt_point;  // 挂载点
            FileSystem *fsclass;
            bool valid;
            uint8 dev;
            uint32 first_data_sec; // data所在的第一个扇区
            uint32 data_sec_cnt; // 数据扇区数
            uint32 data_clus_cnt; // 数据簇数
            uint32 byts_per_clus; // 每簇字节数
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
            SuperBlock(shared_ptr<DEntry> a_root, shared_ptr<fs::DEntry> a_mnt, FileSystem *a_fsclass, uint8 a_dev, uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, BPB a_bpb):fs::SuperBlock(), root(a_root), mnt_point(a_mnt), fsclass(a_fsclass), valid(true), dev(a_dev), first_data_sec(a_fds), data_sec_cnt(a_dsc), data_clus_cnt(a_dcc), byts_per_clus(a_bpc), bpb(a_bpb) {}
            SuperBlock(shared_ptr<DEntry> a_root, shared_ptr<fs::DEntry> a_mnt, FileSystem *a_fsclass, uint8 a_dev, uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):fs::SuperBlock(), root(a_root), mnt_point(a_mnt), fsclass(a_fsclass), valid(true), dev(a_dev), first_data_sec(a_fds), data_sec_cnt(a_dsc), data_clus_cnt(a_dcc), byts_per_clus(a_bpc), bpb(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc) {}
            SuperBlock(shared_ptr<DEntry> a_root, shared_ptr<fs::DEntry> a_mnt, FileSystem *a_fsclass, uint8 a_dev, const BPB& a_bpb):fs::SuperBlock(), root(a_root), mnt_point(a_mnt), fsclass(a_fsclass), valid(true), dev(a_dev), first_data_sec(a_bpb.rsvd_sec_cnt+a_bpb.fat_cnt*a_bpb.fat_sz), data_sec_cnt(a_bpb.tot_sec-first_data_sec), data_clus_cnt(data_sec_cnt/a_bpb.sec_per_clus), byts_per_clus(a_bpb.sec_per_clus*a_bpb.byts_per_sec), bpb(a_bpb) {}
            SuperBlock(shared_ptr<DEntry> a_root, shared_ptr<fs::DEntry> a_mnt, FileSystem *a_fsclass, uint8 a_dev, const BlockBuf &a_blk):SuperBlock(a_root, a_mnt, a_fsclass, a_dev, BPB(a_blk)) {}
            SuperBlock(shared_ptr<DEntry> a_root, shared_ptr<fs::DEntry> a_mnt, FileSystem *a_fsclass, uint8 a_dev, uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):SuperBlock(a_root, a_mnt, a_fsclass, a_dev, BPB(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc)) {}
            ~SuperBlock() = default;
            SuperBlock& operator=(const SuperBlock& a_spblk) = default;
            uint rwClus(uint32 a_cluster, bool a_iswrite, bool a_usrbuf, uint64 a_buf, uint a_off, uint a_len) const;
            inline uint32 firstSec(uint32 a_cluster) const { return (a_cluster-2)*rSPC() + rFDS(); }
            uint32 fatRead(uint32 a_cluster) const;
            inline uint32 numthSec(uint32 a_cluster, uint8 a_fat_num) const { return rRSC() + (a_cluster<<2) / rBPS() + rFS() * (a_fat_num-1); }
            inline uint32 secOffset(uint32 a_cluster) const { return (a_cluster<<2) % rBPS(); }
            void clearClus(uint32 a_cluster) const;
            int fatWrite(uint32 a_cluster, uint32 a_content) const;
            inline void freeClus(uint32 a_cluster) const { fatWrite(a_cluster, 0); }
            inline uint32 rFDS() const { return first_data_sec; }
            inline uint32 rDSC() const { return data_sec_cnt; }
            inline uint32 rDCC() const { return data_clus_cnt; }
            inline uint32 rBPC() const { return byts_per_clus; }
            inline BPB rBPB() const { return bpb; }
            inline uint16 rBPS() const { return bpb.byts_per_sec; }
            inline uint8 rSPC() const { return bpb.sec_per_clus; }
            inline uint16 rRSC() const { return bpb.rsvd_sec_cnt; }
            inline uint8 rFC() const { return bpb.fat_cnt; }
            inline uint32 rHS() const { return bpb.hidd_sec; }
            inline uint32 rTS() const { return bpb.tot_sec; }
            inline uint32 rFS() const { return bpb.fat_sz; }
            inline uint32 rRC() const { return bpb.root_clus; }
            inline uint8 rDev() const { return dev; }
            inline shared_ptr<fs::DEntry> getRoot() const;
            inline shared_ptr<fs::DEntry> getMntPoint() const { return mnt_point; }
            inline fs::FileSystem *getFS() const;
            inline DirEnt *getFATRoot() const;
            inline bool isValid() const { return valid; }
            void unInstall();
    };
    class FileSystem:public fs::FileSystem {
        private:
            string fstype;  // 文件系统类型
            shared_ptr<SuperBlock> spblk;  // 超级块
            bool isroot;  //是否是根文件系统
            string key;
        public:
            FileSystem() = default;
            FileSystem(const FileSystem& a_fs) = default;
            // FileSystem(shared_ptr<SuperBlock> a_spblk, bool a_isroot, uint8 a_key):fs::FileSystem(), fstype("fat32"), spblk(a_spblk), isroot(a_isroot), key(a_key) {}
            FileSystem(bool a_isroot, string a_key):fs::FileSystem(), fstype("fat32"), spblk(nullptr), isroot(a_isroot), key(a_key) {}
            ~FileSystem() = default;
            FileSystem& operator=(const FileSystem& a_fs) = default;
            inline string rFSType() const { return fstype; }
            inline string rKey() const { return key; }
            inline bool isRootFS() const { return isroot; }
            inline shared_ptr<fs::SuperBlock> getSpBlk() const { return spblk; }
            int ldSpBlk(uint8 a_dev, shared_ptr<fs::DEntry> a_mnt);
            void unInstall();
            inline long rMagic() const { return MSDOS_SUPER_MAGIC; }
            inline long rBlkSiz() const { return spblk->rBPS(); }
            inline long rBlkNum() const { return spblk->rTS(); }
            inline long rBlkFree() const { return rBlkNum(); }  // @todo: 未实现
            inline long rMaxFile() const { return rBlkNum()/spblk->rSPC(); }
            inline long rFreeFile() const { return rBlkFree()/spblk->rSPC(); }
            virtual long rNameLen() const { return FAT32_MAX_FILENAME; }
    };
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
            // uint8 dev;   // 设备号
            shared_ptr<SuperBlock> spblk;  // 超级块
            shared_ptr<fs::SuperBlock> mntblk;  // 挂载在该目录下的超级块
            bool dirty;  // 浊/清
            short valid;  // 合法性
            int ref; // 关联数
            uint32 off; // 父目录的entry的偏移 // offset in the parent dir entry, for writing convenience
            DirEnt *parent; // because FAT32 doesn't have such thing like inum, use this for cache trick
            DirEnt *next; // 
            DirEnt *prev; // 
            bool mount_flag;
        // public:
            DirEnt() = default;
            DirEnt(const DirEnt& a_entry):filename(), attribute(a_entry.attribute), first_clus(a_entry.first_clus), file_size(a_entry.file_size), cur_clus(a_entry.cur_clus), clus_cnt(a_entry.clus_cnt), spblk(a_entry.spblk), mntblk(nullptr), dirty(a_entry.dirty), valid(a_entry.valid), ref(a_entry.ref), off(a_entry.off), parent(a_entry.parent), next(a_entry.next), prev(a_entry.prev), mount_flag(a_entry.mount_flag) { strncpy(filename, a_entry.filename, FAT32_MAX_FILENAME); }
            DirEnt(const char *a_name, uint8 a_attr, uint32 a_first_clus, shared_ptr<SuperBlock> a_spblk, DirEnt *a_next, DirEnt *a_prev):filename(), attribute(a_attr), first_clus(a_first_clus), file_size(0), cur_clus(first_clus), clus_cnt(0), spblk(a_spblk), mntblk(nullptr), dirty(false), valid(1), ref(1), off(0), parent(nullptr), next(a_next), prev(a_prev), mount_flag(false) { strncpy(filename, a_name, FAT32_MAX_FILENAME); }
            ~DirEnt() { ref = 0; valid = 0; }
            DirEnt& operator=(const DirEnt& a_entry);
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
            int entLink(DirEnt *a_entry) const;
            int entUnlink() const;
    };
    struct Link{
        union Ent de;
        uint32 link_count;
    };
    class INode:public fs::INode {
        private:
            uint32 inode_num;
            DirEnt *entry;
            inline void nodRelse() const { if(entry != nullptr) { entry->entRelse(); } }
            inline DirEnt *nodDup() const { return entry==nullptr ? nullptr : entry->entDup(); }
            inline DirEnt *nodDup(DirEnt *a_entry) const { return a_entry==nullptr ? nullptr : a_entry->entDup(); }
            inline void nodPanic() const { if(entry == nullptr) { panic("INode panic!\n"); } }
        public:
            INode():fs::INode(), inode_num(0), entry(nullptr) {}
            INode(const INode& a_inode) = default;
            INode(DirEnt *a_entry):fs::INode(), inode_num(a_entry->first_clus), entry(a_entry) {}
            ~INode() { nodRelse(); }
            inline INode& operator=(const INode& a_inode) { nodRelse(); inode_num = a_inode.inode_num; entry = a_inode.nodDup(); return *this; }
            inline INode& operator=(DirEnt *a_entry) { nodRelse(); inode_num = a_entry->first_clus; entry = nodDup(a_entry); return *this; }
            inline void nodRemove() { nodPanic(); entry->entRemove(); }
            inline int nodHardLink(INode *a_inode) { nodPanic(); return entry->entLink(a_inode->entry); }
            inline int nodHardUnlink() { nodPanic(); return entry->entUnlink(); }
            inline void nodTrunc() { nodPanic(); entry->entTrunc(); }
            inline int nodRead(bool a_usrdst, uint64 a_dst, uint a_off, uint a_len) { nodPanic(); return entry->entRead(a_usrdst, a_dst, a_off, a_len); }
            inline int nodWrite(bool a_usrsrc, uint64 a_src, uint a_off, uint a_len) { nodPanic(); return entry->entWrite(a_usrsrc, a_src, a_off, a_len); }
            inline int readLink(char *a_buf, size_t a_bufsiz) { Log(error,"FAT32 does not support readlink\n"); return -EPERM; }
            inline void setMntBlk(shared_ptr<fs::SuperBlock> a_spblk) { nodPanic(); entry->mntblk = a_spblk; }
            inline void unInstall() { nodPanic(); entry->entRelse(); entry->spblk.reset(); entry->mntblk.reset(); entry = nullptr; }
            inline uint8 rAttr() const { nodPanic(); return entry->attribute; }
            inline uint8 rDev() const { nodPanic(); return entry->spblk->rDev(); }
            inline uint32 rFileSize() const { nodPanic(); return entry->file_size; }
            inline uint32 rINo() const { nodPanic(); return inode_num; }
            inline shared_ptr<fs::SuperBlock> getSpBlk() const { nodPanic(); return entry->mntblk==nullptr ? entry->spblk : entry->mntblk; }
            inline DirEnt *rawPtr() const { return nodDup(); }
    };
    class DEntry:public fs::DEntry {
        private:
            shared_ptr<INode> inode;
            DirEnt *entry;
            inline void dERelse() const { if(entry != nullptr) { entry->entRelse(); } }
            inline void dEPanic() const { if(entry == nullptr) { panic("DEntry panic!\n"); } }
        public:
            DEntry():fs::DEntry(), inode(nullptr), entry(nullptr) {}
            DEntry(const DEntry& a_dentry):fs::DEntry(), inode(a_dentry.inode), entry(a_dentry.inode->rawPtr()) {}
            DEntry(shared_ptr<INode> a_inode):fs::DEntry(), inode(a_inode), entry(a_inode->rawPtr()) {}
            DEntry(DirEnt *a_entry):fs::DEntry(), inode(make_shared<INode>(a_entry)), entry(inode->rawPtr()) {}
            ~DEntry() { dERelse(); }
            inline DEntry& operator=(const DEntry& a_dentry) { dERelse(); inode = a_dentry.inode; entry = a_dentry.inode->rawPtr(); return *this; }
            inline DEntry& operator=(shared_ptr<INode> a_inode) { dERelse(); inode = a_inode; entry = a_inode->rawPtr(); return *this; }
            inline DEntry& operator=(DirEnt *a_entry) { dERelse(); inode = make_shared<INode>(a_entry), entry = inode->rawPtr(); return *this; }
            inline shared_ptr<fs::DEntry> entSearch(string a_dirname, uint *a_off = nullptr) { dEPanic(); DirEnt *ret = entry->entSearch(a_dirname, a_off); return ret==nullptr ? nullptr : make_shared<DEntry>(ret); }
            inline shared_ptr<fs::DEntry> entCreate(string a_name, int a_attr) { dEPanic(); DirEnt *ret = entry->entCreate(a_name, a_attr); return ret==nullptr ? nullptr : make_shared<DEntry>(ret); }
            inline int entSymLink(string a_target) { Log(error,"FAT32 does not support symlink\n"); return -EPERM; }
            inline void setMntPoint(const fs::FileSystem *a_fs) { dEPanic(); entry->mount_flag = true; inode->setMntBlk(a_fs->getSpBlk()); }
            inline void clearMnt() { dEPanic(); entry->mount_flag = false; inode->setMntBlk(inode->rawPtr()->spblk); }
            inline void unInstall() { dEPanic(); entry->entRelse(); entry = nullptr; inode->unInstall(); inode.reset(); }
            inline int chMod(mode_t a_mode) { Log(error,"FAT32 does not support chmod\n"); return -EPERM; }
            inline int chOwn(uid_t a_owner, gid_t a_group) { Log(error,"FAT32 does not support chown\n"); return -EPERM; }
            inline const char *rName() const { dEPanic(); return entry->filename; }
            inline shared_ptr<fs::DEntry> getParent() const { dEPanic(); return entry->parent==nullptr ? inode->getSpBlk()->getMntPoint() : make_shared<DEntry>(entry->parent); }
            inline shared_ptr<fs::INode> getINode() const { return inode; }
            inline bool isMntPoint() const { dEPanic(); return entry->mount_flag; }
            inline bool isRoot() const { dEPanic(); return inode->rawPtr()->parent == nullptr; }
            inline bool isEmpty() const { dEPanic(); return entry->isEmpty(); }
            inline DirEnt *rawPtr() const { return inode==nullptr ? nullptr : inode->rawPtr(); }
    };
}

#endif