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
    struct FileSystem {
        uint32 first_data_sec; // data所在的第一个扇区
        uint32 data_sec_cnt; // 数据扇区数
        uint32 data_clus_cnt; // 数据簇数
        uint32 byts_per_clus; // 每簇字节数
        struct { 
            uint16 byts_per_sec;  // 扇区字节数
            uint8 sec_per_clus;  // 每簇扇区数
            uint16 rsvd_sec_cnt;  // 保留扇区数
            uint8 fat_cnt;  // fat数          
            uint32 hidd_sec;  // 隐藏扇区数         
            uint32 tot_sec;  // 总扇区数          
            uint32 fat_sz;   // 一个fat所占扇区数           
            uint32 root_clus; // 根目录簇号 
        } bpb;
        int vaild;
        struct DirEnt root; //这个文件系统的根目录文件
        uint8 mount_mode; //是否被挂载的标志
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
    struct DirEnt *dirLookUp(struct DirEnt *entry, char *filename, uint *poff);
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