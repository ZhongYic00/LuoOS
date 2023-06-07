#ifndef FAT_HH__
#define FAT_HH__

#include "fs.hh"
#include "stat.h"

#define ATTR_READ_ONLY      0x01  // 只读
#define ATTR_HIDDEN         0x02  // 隐藏
#define ATTR_SYSTEM         0x04  // 系统
#define ATTR_VOLUME_ID      0x08  // 卷标
#define ATTR_DIRECTORY      0x10  // 目录
#define ATTR_ARCHIVE        0x20  // 文档
#define ATTR_LONG_NAME      0x0F  // 长名
#define ATTR_LINK           0x40  // link

#define LAST_LONG_ENTRY     0x40  // 最后一个长文件名目录
#define FAT32_EOC           0x0ffffff8  // 
#define EMPTY_ENTRY         0xe5
#define END_OF_ENTRY        0x00
#define CHAR_LONG_NAME      13
#define CHAR_SHORT_NAME     11

#define FAT32_MAX_FILENAME  255
#define FAT32_MAX_PATH      260
#define ENTRY_CACHE_NUM     50

namespace fs {
    
    struct mapped_file {
        uint64 baseaddr;
        unsigned long len;
        SharedPtr<File> mfile;
        int valid;
        long off;
    };
    // 短文件名(32bytes)
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
    // 长文件名(32bytes)
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
    // disk entry
    union Ent {
        SNE sne; // short name entry
        LNE lne; // long name entry
    };
    // 目录项
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
    // link
    struct link{
        union Ent de;
        uint32 link_count;
    };
    // file system?
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


    int fat32Init(void);
    struct DirEnt *dirlookup(struct DirEnt *entry, char *filename, uint *poff);
    char* formatname(char *name);
    void emake(struct DirEnt *dp, struct DirEnt *ep, uint off);
    struct DirEnt *ealloc(struct DirEnt *dp, char *name, int attr);
    struct DirEnt *edup(struct DirEnt *entry);
    void eupdate(struct DirEnt *entry);
    void etrunc(struct DirEnt *entry);
    void eremove(struct DirEnt *entry);
    void eput(struct DirEnt *entry);
    void estat(struct DirEnt *ep, struct stat *st);
    void elock(struct DirEnt *entry);
    void eunlock(struct DirEnt *entry);
    int enext(struct DirEnt *dp, struct DirEnt *ep, uint off, int *count);
    struct DirEnt *ename(char *path);
    struct DirEnt *enameparent(char *path, char *name);
    int eread(struct DirEnt *entry, int user_dst, uint64 dst, uint off, uint n);
    int ewrite(struct DirEnt *entry, int user_src, uint64 src, uint off, uint n);
    struct DirEnt *enameparent2(char *path, char *name, SharedPtr<File> f);
    struct DirEnt *ename2(char *path, SharedPtr<File> f);
    uint32 get_byts_per_clus();
    int link(char* oldpath, SharedPtr<File> f1, char* newpath, SharedPtr<File> f2);
    int unlink(char *path, SharedPtr<File> f);
    int remove(char *path);
    int isdirempty(struct DirEnt *dp);
    int remove2(char *path, SharedPtr<File> f);
    int syn_disk(uint64 start,long len);
    int do_mount(struct DirEnt *mountpoint,struct DirEnt *dev);
    int do_umount(struct DirEnt *mountpoint);
    struct DirEnt *create(char *path, short type, int mode);
    struct DirEnt *create2(char *path, short type, int mode, SharedPtr<File> f);
    void getDStat(struct DirEnt *de, struct dstat *st);
    void getKStat(struct DirEnt *de, struct kstat *kst);

}

#endif