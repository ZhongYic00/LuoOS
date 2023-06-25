#ifndef FAT_HH__
#define FAT_HH__

#include "fs.hh"

namespace fs {
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
    } __attribute__((packed, aligned(4))) SNE;
    typedef struct LongNameEntry {
        uint8 order; // 目录项序列号
        wchar name1[5]; // 第1~5个字符的unicode码
        uint8 attr; // 长目录项的属性标志，一定是0x0F。
        uint8 _type; // 系统保留
        uint8 checksum; // 校验和
        wchar name2[6]; // 第6~11个字符的unicode码
        uint16 _fst_clus_lo; // 文件起始簇号 保留 目前常置0
        wchar name3[2]; // 第12~13个字符的unicode码
    } __attribute__((packed, aligned(4))) LNE;
    union Ent {
        SNE sne; // short name entry
        LNE lne; // long name entry
    };
    struct DirEnt {
        char  filename[FAT32_MAX_FILENAME + 1];  // 文件名
        uint8   attribute;  // 属性
        uint32  first_clus;  // 起始簇号
        uint32  file_size;  // 文件大小
        uint32  cur_clus;  // 当前簇号
        uint clus_cnt;  // 当前簇是该文件的第几个簇
        uint8 dev;   // 设备号
        uint8 dirty;  // 浊/清
        short valid;  // 合法性
        int ref; // 关联数
        uint32 off; // 父目录的entry的偏移 // offset in the parent dir entry, for writing convenience
        struct DirEnt *parent; // because FAT32 doesn't have such thing like inum, use this for cache trick
        struct DirEnt *next; // 
        struct DirEnt *prev; // 
        uint8 mount_flag;
        // struct sleeplock lock;
    };
    struct Link{
        union Ent de;
        uint32 link_count;
    };
    class FileSystem;
    class SuperBlock {
        private:
            // friend class FileSystem;
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
                BPB_t() {}
                BPB_t(const BPB_t& a_bpb):byts_per_sec(a_bpb.byts_per_sec), sec_per_clus(a_bpb.sec_per_clus), rsvd_sec_cnt(a_bpb.rsvd_sec_cnt), fat_cnt(a_bpb.fat_cnt), hidd_sec(a_bpb.hidd_sec), tot_sec(a_bpb.tot_sec), fat_sz(a_bpb.fat_sz), root_clus(a_bpb.root_clus) {}
                BPB_t(uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):byts_per_sec(a_bps), sec_per_clus(a_spc), rsvd_sec_cnt(a_rsc), fat_cnt(a_fc), hidd_sec(a_hs), tot_sec(a_ts), fat_sz(a_fs), root_clus(a_rc) {}
                ~BPB_t() {}
                const BPB_t& operator=(const BPB_t& a_bpb) {
                    memmove((void*)this, (void*)&a_bpb, sizeof(*this));
                    return *this;
                }
            } bpb;
        public:
            SuperBlock() {}
            SuperBlock(const SuperBlock& a_spblk):first_data_sec(a_spblk.first_data_sec), data_sec_cnt(a_spblk.data_sec_cnt), data_clus_cnt(a_spblk.data_clus_cnt), byts_per_clus(a_spblk.byts_per_clus), bpb(a_spblk.bpb) {}
            SuperBlock(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, BPB_t a_bpb):first_data_sec(a_fds), data_sec_cnt(a_dsc), data_clus_cnt(a_dcc), byts_per_clus(a_bpc), bpb(a_bpb) {}
            SuperBlock(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):first_data_sec(a_fds), data_sec_cnt(a_dsc), data_clus_cnt(a_dcc), byts_per_clus(a_bpc), bpb(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc) {}
            SuperBlock(BPB_t a_bpb):first_data_sec(a_bpb.rsvd_sec_cnt+a_bpb.fat_cnt*a_bpb.fat_sz), data_sec_cnt(a_bpb.tot_sec-first_data_sec), data_clus_cnt(data_sec_cnt/a_bpb.sec_per_clus), byts_per_clus(a_bpb.sec_per_clus*a_bpb.byts_per_sec), bpb(a_bpb) {}
            SuperBlock(uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc):first_data_sec(a_rsc+a_fc*a_fs), data_sec_cnt(a_ts-first_data_sec), data_clus_cnt(data_sec_cnt/a_spc), byts_per_clus(a_spc*a_bps), bpb(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc) {}
            ~SuperBlock() {}
            const SuperBlock& operator=(const SuperBlock& a_spblk) {
                memmove((void*)this, (void*)&a_spblk, sizeof(*this));
                return *this;
            }
            inline const uint32 rFDS() const { return first_data_sec; }
            inline const uint32 rDSC() const { return data_sec_cnt; }
            inline const uint32 rDCC() const { return data_clus_cnt; }
            inline const uint32 rBPC() const { return byts_per_clus; }
            inline const BPB_t rBPB() const { return bpb; }
            inline const uint16 rBPS() const { return bpb.byts_per_sec; }
            inline const uint8 rSPC() const { return bpb.sec_per_clus; }
            inline const uint16 rRSC() const { return bpb.rsvd_sec_cnt; }
            inline const uint8 rFC() const { return bpb.fat_cnt; }
            inline const uint32 rHS() const { return bpb.hidd_sec; }
            inline const uint32 rTS() const { return bpb.tot_sec; }
            inline const uint32 rFS() const { return bpb.fat_sz; }
            inline const uint32 rRC() const { return bpb.root_clus; }
    };
    class FileSystem {
        private:
            SuperBlock spblk;
            bool valid;
            struct DirEnt root; //这个文件系统的根目录文件
            uint8 mount_mode; //是否被挂载的标志
        public:
            FileSystem() {}
            FileSystem(const FileSystem& a_fs):spblk(a_fs.spblk), valid(a_fs.valid), root(a_fs.root), mount_mode(a_fs.mount_mode) {}
            FileSystem(SuperBlock a_spblk, bool a_valid, struct DirEnt a_root, uint8 a_mm):spblk(a_spblk), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, typeof(spblk.rBPB()) a_bpb, bool a_valid, struct DirEnt a_root, uint8 a_mm):spblk(a_fds, a_dsc, a_dcc, a_bpc, a_bpb), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(uint32 a_fds, uint32 a_dsc, uint32 a_dcc, uint32 a_bpc, uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc, bool a_valid, struct DirEnt a_root, uint8 a_mm):spblk(a_fds, a_dsc, a_dcc, a_bpc, a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(typeof(spblk.rBPB()) a_bpb, bool a_valid, struct DirEnt a_root, uint8 a_mm):spblk(a_bpb), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            FileSystem(uint16 a_bps, uint8 a_spc, uint16 a_rsc, uint8 a_fc, uint32 a_hs, uint32 a_ts, uint32 a_fs, uint32 a_rc, bool a_valid, struct DirEnt a_root, uint8 a_mm):spblk(a_bps, a_spc, a_rsc, a_fc, a_hs, a_ts, a_fs, a_rc), valid(a_valid), root(a_root), mount_mode(a_mm) {}
            ~FileSystem() {}
            const FileSystem& operator=(const FileSystem& a_fs) {
                memmove((void*)this, (void*)&a_fs, sizeof(*this));
                return *this;
            }
            inline const SuperBlock rSpBlk() const { return spblk; }
            inline const uint32 rFDS() const { return spblk.rFDS(); }
            inline const uint32 rDSC() const { return spblk.rDSC(); }
            inline const uint32 rDCC() const { return spblk.rDCC(); }
            inline const uint32 rBPC() const { return spblk.rBPC(); }
            inline const typeof(spblk.rBPB()) rBPB() const { return spblk.rBPB(); }
            inline const uint16 rBPS() const { return spblk.rBPS(); }
            inline const uint8 rSPC() const { return spblk.rSPC(); }
            inline const uint16 rRSC() const { return spblk.rRSC(); }
            inline const uint8 rFC() const { return spblk.rFC(); }
            inline const uint32 rHS() const { return spblk.rHS(); }
            inline const uint32 rTS() const { return spblk.rTS(); }
            inline const uint32 rFS() const { return spblk.rFS(); }
            inline const uint32 rRC() const { return spblk.rRC(); }
            inline const bool isValid() const { return valid; }
            inline struct DirEnt *const getRoot() { return &root; }
            inline const struct DirEnt *const findRoot() const { return &root; }
            inline const uint8 rMM() const { return mount_mode; }
    };
    using eastl::string;
    using eastl::vector
    class Path {
        private:
            string pathname;
            vector<string> dirname;
        public:
            Path():pathname(), dirname(), dirnum(0) {}
            Path(const Path& a_path):pathname(a_path.pathname), dirname(a_path.dirname) {}
            Path(const string& a_str):pathname(a_str), dirname() {
                size_t len = pathname.length();
                if(len > 0) {  // 保证数组长度不为0
                    auto ind = new size_t[len][2];
                    bool rep = true;
                    int dirnum = 0;
                    for(size_t i = 0; i < len; ++i) {  // 识别以'/'结尾的目录
                        if(pathname[i] == '/') {
                            if(!rep) {
                                rep = true;
                                ++dirnum;
                            }
                        }
                        else {
                            ++(ind[dirnum][1]);
                            if(rep) {
                                rep = false;
                                ind[dirnum][0] = i;
                            }
                        }
                    }
                    if(!rep) { ++dirnum; }  // 补齐末尾'/'
                    dirname = vector<string>(dirnum);
                    for(size_t i = 0; i < dirnum; ++i) { dirname[i] = pathname.substr(ind[dirnum][0], ind[dirnum][1]); }
                    delete[] ind;
                }
            }
            const Path& operator=(const Path& a_path) {
                pathname = a_path.pathname;
                dirname = a_path.dirname;
                return *this;
            }
            // ~Path() { delete dirname; }
            struct DirEnt *pathSearch(SharedPtr<File> a_file, bool a_parent) const;  // @todo 改成返回File
            inline struct DirEnt *pathSearch(SharedPtr<File> a_file) const { return pathSearch(a_file, 0); }
            inline struct DirEnt *pathSearch(bool a_parent) const { return pathSearch(nullptr, a_parent); }
            inline struct DirEnt *pathSearch() const { return pathSearch(nullptr, 0); }
    };
	struct DStat {
	  uint64 d_ino;	// 索引结点号
	  int64 d_off;	// 到下一个dirent的偏移
	  uint16 d_reclen;	// 当前dirent的长度
	  uint8 d_type;	// 文件类型
	  char d_name[STAT_MAX_NAME + 1];	//文件名
	};
	struct Stat {
	  char name[STAT_MAX_NAME + 1]; // 文件名
	  int dev;     // File system's disk device // 文件系统的磁盘设备
	  short type;  // Type of file // 文件类型
	  uint64 size; // Size of file in bytes // 文件大小(字节)
	};
	struct KStat {
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
	};


    int fat32Init(void);
    struct DirEnt *dirLookUp(struct DirEnt *entry, const char *filename, uint *poff);
    char* flName(char *name);
    void entSynAt(struct DirEnt *dp, struct DirEnt *ep, uint off);
    struct DirEnt *entCreateAt(struct DirEnt *dp, char *name, int attr);
    struct DirEnt *entDup(struct DirEnt *entry);
    void dirUpdate(struct DirEnt *entry);
    void entTrunc(struct DirEnt *entry);
    void entRemove(struct DirEnt *entry);
    void entRelse(struct DirEnt *entry);
    void entStat(struct DirEnt *ep, struct Stat *st);
    void entLock(struct DirEnt *entry);
    void entUnlock(struct DirEnt *entry);
    int entFindNext(struct DirEnt *dp, struct DirEnt *ep, uint off, int *count);
    struct DirEnt *entEnter(char *path);
    struct DirEnt *entEnterParent(char *path, char *name);
    int entRead(struct DirEnt *entry, int user_dst, uint64 dst, uint off, uint n);
    int entWrite(struct DirEnt *entry, int user_src, uint64 src, uint off, uint n);
    struct DirEnt *entEnterParentAt(char *path, char *name, SharedPtr<File> f);
    struct DirEnt *entEnterFrom(char *path, SharedPtr<File> f);
    uint32 getBytesPerClus();
    int entLink(char* oldpath, SharedPtr<File> f1, char* newpath, SharedPtr<File> f2);
    int entUnlink(char *path, SharedPtr<File> f);
    int pathRemove(char *path);
    int dirIsEmpty(struct DirEnt *dp);
    int pathRemoveAt(char *path, SharedPtr<File> f);
    int devMount(struct DirEnt *mountpoint,struct DirEnt *dev);
    int devUnmount(struct DirEnt *mountpoint);
    struct DirEnt *pathCreate(char *path, short type, int mode);
    struct DirEnt *pathCreateAt(char *path, short type, int mode, SharedPtr<File> f);
    void getDStat(struct DirEnt *de, struct DStat *st);
    void getKStat(struct DirEnt *de, struct KStat *kst);

}

#endif