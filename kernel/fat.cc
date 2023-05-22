#include "fat.hh"
#include "param.h"
#include "buf.h"
#include "stat.h"
#include "kernel.hh"

using namespace fs;

///////////////FAT依赖的其它接口，需要在FAT的代码中替换成等价接口/////////////////
// todo: 实现原接口的等价功能并替换原接口
/*
    Copy to either a user address, or kernel address,
    depending on usr_dst.
    Returns 0 on success, -1 on error.
    todo: 使用我们的函数在FAT的代码中替换该接口
    来自xv6的proc.c，仅供替换时参考，完成替换后要删掉
*/
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
//   if(user_dst){ return copyout2(dst, src, len); }
//   else {
//     memmove((char *)dst, src, len);
//     return 0;
//   }
    return 0;
}
/*
    Copy from either a user address, or kernel address,
    depending on usr_src.
    Returns 0 on success, -1 on error.
    todo: 使用我们的函数实现替换该接口
    来自xv6的proc.c，仅供替换时参考，完成替换后要删掉
*/
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
//   if(user_src){ return copyin2(dst, src, len); }
//   else {
//     memmove(dst, (char*)src, len);
//     return 0;
//   }
    return 0;
}
// todo: 使用我们的函数实现替换该接口
// 来自xv6的vm.c，仅供替换时参考，完成替换后要删掉
// pte_t * walk(pagetable_t pagetable, uint64 va, int alloc) {
// //   if(va >= MAXVA) { panic("walk"); }
// //   for(int level = 2; level > 0; level--) {
// //     pte_t *pte = &pagetable[PX(level, va)];
// //     if(*pte & PTE_V) { pagetable = (pagetable_t)PTE2PA(*pte); }
// //     else {
// //       if(!alloc || (pagetable = (pde_t*)kalloc()) == NULL) { return NULL; }
// //       memset(pagetable, 0, PGSIZE);
// //       *pte = PA2PTE(pagetable) | PTE_V;
// //     }
// //   }
// //   return &pagetable[PX(0, va)];
//     return NULL;
// }
///////////////////FAT////////////////////
// todo: 把FAT中页表和进程相关的代码全部改成适用于我们项目的形式
static struct {
    uint32 first_data_sec; // data所在的第一个扇区
    uint32 data_sec_cnt; // 数据扇区数
    uint32 data_clus_cnt; // 数据簇数
    uint32 byts_per_clus; // 每簇字节数
    struct {
        uint16  byts_per_sec;  // 扇区字节数
        uint8   sec_per_clus;  // 每簇扇区数
        uint16  rsvd_sec_cnt;  // 保留扇区数
        uint8   fat_cnt;  // fat数          
        uint32  hidd_sec;  // 隐藏扇区数         
        uint32  tot_sec;  // 总扇区数          
        uint32  fat_sz;   // 一个fat所占扇区数           
        uint32  root_clus; // 根目录簇号 
    } bpb;
} fat;
static struct entry_cache {
    // struct spinlock lock;
    struct dirent entries[ENTRY_CACHE_NUM];
} ecache; // 目录缓冲区
static struct dirent root; // 根目录
struct fstype dev_fat[8]; //挂载设备集合
int mount_num=0; //表示寻找在挂载集合的下标
// @param   cluster   cluster number starts from 2, which means no 0 and 1
// 根据簇号得到起始扇区号
static inline uint32 first_sec_of_clus(uint32 cluster) { return ((cluster - 2) * fat.bpb.sec_per_clus) + fat.first_data_sec; }
/*
    For the given number of a data cluster, return the number of the sector in a FAT table.
    @param   cluster     number of a data cluster
    @param   fat_num     number of FAT table from 1, shouldn't be larger than bpb::fat_cnt
*/
// 根据簇号得到在第fat_num个FAT表中的扇区号
static inline uint32 fat_sec_of_clus(uint32 cluster, uint8 fat_num) {
    return fat.bpb.rsvd_sec_cnt + (cluster << 2) / fat.bpb.byts_per_sec + fat.bpb.fat_sz * (fat_num - 1);
}
/*
    For the given number of a data cluster, return the offest in the corresponding sector in a FAT table.
    @param   cluster   number of a data cluster
*/
// 根据簇号得到在FAT中的扇区偏移
static inline uint32 fat_offset_of_clus(uint32 cluster) { return (cluster << 2) % fat.bpb.byts_per_sec; }
/*
    Read the FAT table content corresponded to the given cluster number.
    @param   cluster     the number of cluster which you want to read its content in FAT table
*/
// 根据簇号读取fat1中的信息
static uint32 read_fat(uint32 cluster) {
    if (cluster >= FAT32_EOC) {
        return cluster;
    }
    if (cluster > fat.data_clus_cnt + 1) {     // because cluster number starts at 2, not 0
        return 0;
    }
    uint32 fat_sec = fat_sec_of_clus(cluster, 1);
    // here should be a cache layer for FAT table, but not implemented yet.
    struct buf *b = bread(0, fat_sec);
    uint32 next_clus = *(uint32 *)(b->data + fat_offset_of_clus(cluster));
    brelse(b);
    return next_clus;
}
/*
    Write the FAT region content corresponded to the given cluster number.
    @param   cluster     the number of cluster to write its content in FAT table
    @param   content     the content which should be the next cluster number of FAT end of chain flag
*/
// 根据簇号将内容写入fat1，return 0 成功 1 失败
static int write_fat(uint32 cluster, uint32 content) {
    if (cluster > fat.data_clus_cnt + 1) {
        return -1;
    }
    uint32 fat_sec = fat_sec_of_clus(cluster, 1);
    struct buf *b = bread(0, fat_sec);
    uint off = fat_offset_of_clus(cluster);
    *(uint32 *)(b->data + off) = content;
    bwrite(b);
    brelse(b);
    return 0;
}
// 根据簇号将该簇清空
static void zero_clus(uint32 cluster) {
    uint32 sec = first_sec_of_clus(cluster);
    struct buf *b;
    for (int i = 0; i < fat.bpb.sec_per_clus; i++) {
        b = bread(0, sec++);
        memset(b->data, 0, BSIZE);
        bwrite(b);
        brelse(b);
    }
}
// 分配空闲簇，返回分配的簇号
static uint32 alloc_clus(uint8 dev) {
    // should we keep a free cluster list? instead of searching fat every time.
    struct buf *b;
    uint32 sec = fat.bpb.rsvd_sec_cnt;
    uint32 const ent_per_sec = fat.bpb.byts_per_sec / sizeof(uint32);
    for (uint32 i = 0; i < fat.bpb.fat_sz; i++, sec++) {
        b = bread(dev, sec);
        for (uint32 j = 0; j < ent_per_sec; j++) {
            if (((uint32 *)(b->data))[j] == 0) {
                ((uint32 *)(b->data))[j] = FAT32_EOC + 7;
                bwrite(b);
                brelse(b);
                uint32 clus = i * ent_per_sec + j;
                zero_clus(clus);
                return clus;
            }
        }
        brelse(b);
    }
    panic("no clusters");
}
// 释放簇
static void free_clus(uint32 cluster) { write_fat(cluster, 0); }
/* 
    当write为1：将首地址为data、长为n的数据写入簇号为cluster、偏移为off处
    当write为0：将簇号为cluster、偏移为off处、长为n的数据读到data
    返回写入数据的长度
*/
static uint rw_clus(uint32 cluster, int write, int user, uint64 data, uint off, uint n) {
    if (off + n > fat.byts_per_clus) { panic("offset out of range"); }
    uint tot, m;
    struct buf *bp;
    uint sec = first_sec_of_clus(cluster) + off / fat.bpb.byts_per_sec;
    off = off % fat.bpb.byts_per_sec;
    int bad = 0;
    for (tot = 0; tot < n; tot += m, off += m, data += m, sec++) {
        bp = bread(0, sec);
        m = BSIZE - off % BSIZE;
        if (n - tot < m) { m = n - tot; }
        if (write) {
            if ((bad = either_copyin(bp->data + (off % BSIZE), user, data, m)) != -1) { bwrite(bp); }
        }
        else { bad = either_copyout(user, data, bp->data + (off % BSIZE), m); }
        brelse(bp);
        if (bad == -1) { break; }
    }
    return tot;
}
/*
    for the given entry, relocate the cur_clus field based on the off
    @param   entry       modify its cur_clus field
    @param   off         the offset from the beginning of the relative file
    @param   alloc       whether alloc new cluster when meeting end of FAT chains
    @return              the offset from the new cur_clus
*/
/* 
    将entry->cur_clus移动到off（off文件中的偏移）所在的簇
    如果cur_clus<off所在的簇且不包含off所在的簇
    1.当alloc为1，则为该文件分配簇，直到到达off所在的簇
    2.当alloc为0，则将cur_clus置为first_clus
    返回off在该簇的偏移（off % fat.byts_per_clus）
*/
static int reloc_clus(struct dirent *entry, uint off, int alloc) {
    int clus_num = off / fat.byts_per_clus;
    while (clus_num > entry->clus_cnt) {
        int clus = read_fat(entry->cur_clus);
        if (clus >= FAT32_EOC) {
            if (alloc) {
                clus = alloc_clus(entry->dev);
                write_fat(entry->cur_clus, clus);
            }
            else {
                entry->cur_clus = entry->first_clus;
                entry->clus_cnt = 0;
                return -1;
            }
        }
        entry->cur_clus = clus;
        entry->clus_cnt++;
    }
    if (clus_num < entry->clus_cnt) {
        entry->cur_clus = entry->first_clus;
        entry->clus_cnt = 0;
        while (entry->clus_cnt < clus_num) {
            entry->cur_clus = read_fat(entry->cur_clus);
            if (entry->cur_clus >= FAT32_EOC) {
                panic("reloc_clus");
            }
            entry->clus_cnt++;
        }
    }
    return off % fat.byts_per_clus;
}
/*
    Returns a dirent struct. If name is given, check ecache. It is difficult to cache entries
    by their whole path. But when parsing a path, we open all the directories through it, 
    which forms a linked list from the final file to the root. Thus, we use the "parent" pointer 
    to recognize whether an entry with the "name" as given is really the file we want in the right path.
    Should never get root by eget, it's easy to understand.
*/
// 在目录缓冲区中根据parent和name寻找目录，如果没有找到则将其放到缓冲区，并将vaild设为0（如果缓冲区有空余的话）（没有置换算法）
static struct dirent *eget(struct dirent *parent, char *name) {
    struct dirent *ep;
    // acquire(&ecache.lock);
    if (name) {
        for (ep = root.next; ep != &root; ep = ep->next) {          // LRU algo
            if (ep->valid == 1 && ep->parent == parent
                && strncmp(ep->filename, name, FAT32_MAX_FILENAME) == 0) {
                if (ep->ref++ == 0) {
                    ep->parent->ref++;
                }
                // release(&ecache.lock);
                return ep;
            }
        }
    }
    for (ep = root.prev; ep != &root; ep = ep->prev) {              // LRU algo
        if (ep->ref == 0) {
            ep->ref = 1;
            ep->dev = parent->dev;
            ep->off = 0;
            ep->valid = 0;
            ep->dirty = 0;
            // release(&ecache.lock);
            return ep;
        }
    }
    panic("eget: insufficient ecache");
    return 0;
}
// 将name转化为短名（小写字母转大写，非法字符变成'_'）
static void generate_shortname(char *shortname, char *name) {
    static char illegal[] = { '+', ',', ';', '=', '[', ']', 0 };   // these are legal in l-n-e but not s-n-e
    int i = 0;
    char c, *p = name;
    for (int j = strlen(name) - 1; j >= 0; j--) {
        if (name[j] == '.') {
            p = name + j; // 最后一个'.'
            break;
        }
    }
    while (i < CHAR_SHORT_NAME && (c = *name++)) {
        // 
        if (i == 8 && p) {  
            if (p + 1 < name) { break; }            // no '.'
            else {
                name = p + 1, p = 0;
                continue;
            }
        }
        
        if (c == ' ') { continue; }
        if (c == '.') {
            if (name > p) {                    // last '.'
                memset(shortname + i, ' ', 8 - i);
                i = 8, p = 0;
            }
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c += 'A' - 'a';
        } else {
            if (strchr(illegal, c) != NULL) { c = '_'; }
        }
        shortname[i++] = c;
    }
    while (i < CHAR_SHORT_NAME) { shortname[i++] = ' '; }
}
// 根据shortname计算校验和
uint8 cal_checksum(uchar* shortname) {
    uint8 sum = 0;
    for (int i = CHAR_SHORT_NAME; i != 0; i--) { sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *shortname++; }
    return sum;
}
/*
    Read filename from directory entry.
    @param   buffer      pointer to the array that stores the name
    @param   raw_entry   pointer to the entry in a sector buffer
    @param   islong      if non-zero, read as l-n-e, otherwise s-n-e.
*/
// 对于给定的目录项读取文件名，放在buffer中
static void read_entry_name(char *buffer, union dentry *d) {
    if (d->lne.attr == ATTR_LONG_NAME) {                       // long entry branch
        wchar temp[NELEM(d->lne.name1)];
        memmove(temp, d->lne.name1, sizeof(temp));
        snstr(buffer, temp, NELEM(d->lne.name1));
        buffer += NELEM(d->lne.name1);
        snstr(buffer, d->lne.name2, NELEM(d->lne.name2));
        buffer += NELEM(d->lne.name2);
        snstr(buffer, d->lne.name3, NELEM(d->lne.name3));
    }
    else {
        // assert: only "." and ".." will enter this branch
        memset(buffer, 0, CHAR_SHORT_NAME + 2); // plus '.' and '\0'
        int i;
        for (i = 0; d->sne.name[i] != ' ' && i < 8; i++) { buffer[i] = d->sne.name[i]; }
        if (d->sne.name[8] != ' ') { buffer[i++] = '.'; }
        for (int j = 8; j < CHAR_SHORT_NAME; j++, i++) {
            if (d->sne.name[j] == ' ') { break; }
            buffer[i] = d->sne.name[j];
        }
    }
}
/*
    Read entry_info from directory entry.
    @param   entry       pointer to the structure that stores the entry info
    @param   raw_entry   pointer to the entry in a sector buffer
*/
// 将目录项d的信息读取到entry
static void read_entry_info(struct dirent *entry, union dentry *d) {
    entry->attribute = d->sne.attr;
    entry->first_clus = ((uint32)d->sne.fst_clus_hi << 16) | d->sne.fst_clus_lo;
    entry->file_size = d->sne.file_size;
    entry->cur_clus = entry->first_clus;
    entry->clus_cnt = 0;
}
// 对于给定的path，读取路径的开头文件名，返回去掉了开头文件名的路径
static char *skipelem(char *path, char *name) {
    while (*path == '/') { path++; }
    if (*path == 0) { return NULL; }
    char *s = path;
    while (*path != '/' && *path != 0) { path++; }
    int len = path - s;
    if (len > FAT32_MAX_FILENAME) { len = FAT32_MAX_FILENAME; }
    name[len] = 0;
    memmove(name, s, len);
    while (*path == '/') { path++; }
    return path;
}
/*
    FAT32 version of namex in xv6's original file system.
    根据路径找到目录，name记录了最后找到的目录名
    对于/a/b/c/d
    1. 如果parent=1，返回d
    2. 如果parent=0，返回d/
*/
static struct dirent *lookup_path(char *path, int parent, char *name) {
    struct dirent *entry, *next;
    if (*path == '/') { entry = edup(&root); }
    else if (*path != '\0') { entry = edup(kHartObjs.curtask->getProcess()->cwd); }
    else { return NULL; }
    while ((path = skipelem(path, name)) != 0) {
        elock(entry);
        if (!(entry->attribute & ATTR_DIRECTORY)) {
            eunlock(entry);
            eput(entry);
            return NULL;
        }
        if (parent && *path == '\0') {
            eunlock(entry);
            return entry;
        }
        if ((next = dirlookup(entry, name, 0)) == 0) {
            eunlock(entry);
            eput(entry);
            return NULL;
        }
        eunlock(entry);
        eput(entry);
        entry = next;
    }
    if (parent) {
        eput(entry);
        return NULL;
    }
    return entry;
}
static struct dirent *lookup_path2(char *path, int parent, struct File *f ,char *name) {
    struct dirent *entry, *next;
    if (*path == '\0') {
        printf("nothing in path\n");
        return NULL;
    }
    else if(*path == '/') { entry = edup(&root); }
    else if(!f) { entry = edup(kHartObjs.curtask->getProcess()->cwd); }
    else { entry = edup(f->ep); }
    while ((path = skipelem(path, name)) != 0) {
        elock(entry);
        if (!(entry->attribute & ATTR_DIRECTORY)) {
            printf("not dir\n");
            eunlock(entry);
            eput(entry);
            return NULL;
        }
        if (parent && *path == '\0') {
            eunlock(entry);
            return entry;
        }
        if ((next = dirlookup(entry, name, 0)) == 0) {
            // printf("can't find %s in %s\n", name, entry->filename);
            eunlock(entry);
            eput(entry);
            return NULL;
        }
        eunlock(entry);
        eput(entry);
        entry = next;
    }
    if (parent) {
        printf("path is wrong\n");
        eput(entry);
        return NULL;
    }
    return entry;
}

/////////////////fs.hh中定义的函数///////////////////////////
int fs::fat32_init() {
    struct buf *b = bread(0, 0);
    if (strncmp((char const*)(b->data + 82), "FAT32", 5)) { panic("not FAT32 volume"); }
    memmove(&fat.bpb.byts_per_sec, b->data + 11, 2); // avoid misaligned load on k210
    fat.bpb.sec_per_clus = *(b->data + 13);  
    fat.bpb.rsvd_sec_cnt = *(uint16 *)(b->data + 14);
    fat.bpb.fat_cnt = *(b->data + 16);
    fat.bpb.hidd_sec = *(uint32 *)(b->data + 28);
    fat.bpb.tot_sec = *(uint32 *)(b->data + 32);
    fat.bpb.fat_sz = *(uint32 *)(b->data + 36);
    fat.bpb.root_clus = *(uint32 *)(b->data + 44);
    fat.first_data_sec = fat.bpb.rsvd_sec_cnt + fat.bpb.fat_cnt * fat.bpb.fat_sz;
    fat.data_sec_cnt = fat.bpb.tot_sec - fat.first_data_sec;
    fat.data_clus_cnt = fat.data_sec_cnt / fat.bpb.sec_per_clus;
    fat.byts_per_clus = fat.bpb.sec_per_clus * fat.bpb.byts_per_sec;
    brelse(b);
    // make sure that byts_per_sec has the same value with BSIZE 
    if (BSIZE != fat.bpb.byts_per_sec) { panic("byts_per_sec != BSIZE"); }
    // initlock(&ecache.lock, "ecache");
    memset(&root, 0, sizeof(root));
    // initsleeplock(&root.lock, "entry");
    root.attribute = (ATTR_DIRECTORY | ATTR_SYSTEM); 
    root.first_clus = root.cur_clus = fat.bpb.root_clus;
    root.valid = 1; 
    root.prev = &root;
    root.next = &root;
    root.filename[0] = '/';
    root.filename[1] = '\0';
    for(struct dirent *de = ecache.entries; de < ecache.entries + ENTRY_CACHE_NUM; de++) {
        de->dev = 0;
        de->valid = 0;
        de->ref = 0;
        de->dirty = 0;
        de->parent = 0;
        de->next = root.next;
        de->prev = &root;
        // initsleeplock(&de->lock, "entry");
        root.next->prev = de;
        root.next = de;
    }
     //将主存的系统存入设备管理
    dev_fat[0].byts_per_clus = fat.byts_per_clus;
    dev_fat[0].data_clus_cnt = fat.data_clus_cnt;
    dev_fat[0].data_sec_cnt = fat.data_sec_cnt;
    dev_fat[0].first_data_sec = fat.first_data_sec;
    dev_fat[0].bpb.byts_per_sec = fat.bpb.byts_per_sec;
    dev_fat[0].bpb.fat_cnt = fat.bpb.fat_cnt;
    dev_fat[0].bpb.fat_sz = fat.bpb.fat_sz;
    dev_fat[0].bpb.hidd_sec = fat.bpb.hidd_sec;
    dev_fat[0].bpb.root_clus = fat.bpb.root_clus;
    dev_fat[0].bpb.rsvd_sec_cnt = fat.bpb.rsvd_sec_cnt;
    dev_fat[0].bpb.sec_per_clus = fat.bpb.sec_per_clus;
    dev_fat[0].bpb.tot_sec = dev_fat->bpb.tot_sec;
    dev_fat[0].mount_mode = 0;
    dev_fat[0].root = root;
    dev_fat[0].vaild = 1;
    return 0;
}
uint32 fs::get_byts_per_clus() { return fat.byts_per_clus; }
/* like the original readi, but "reade" is odd, let alone "writee" */
// Caller must hold entry->lock.
// 读取偏移为off，长为n的数据到dst处，并将cur_clus移动到结束处所在的簇（off是相对于数据区起始位置的偏移）
int eread(struct dirent *entry, int user_dst, uint64 dst, uint off, uint n) {
    if (off > entry->file_size || off + n < off || (entry->attribute & ATTR_DIRECTORY)) { return 0; }
    if (entry->attribute & ATTR_LINK){
        struct link li;
        rw_clus(entry->first_clus, 0, 0, (uint64)&li, 0, 36);
        entry->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        entry->attribute = li.de.sne.attr;
    }
    if (off + n > entry->file_size) { n = entry->file_size - off; }
    uint tot, m;
    for (tot = 0; entry->cur_clus < FAT32_EOC && tot < n; tot += m, off += m, dst += m) {
        reloc_clus(entry, off, 0);
        m = fat.byts_per_clus - off % fat.byts_per_clus;
        if (n - tot < m) { m = n - tot; }
        if (rw_clus(entry->cur_clus, 0, user_dst, dst, off % fat.byts_per_clus, m) != m) { break; }
    }
    return tot;
}
// Caller must hold entry->lock.
// 将首地址为src，长为n的数据写入偏移为off处，并将cur_clus移动到结束处所在的簇（off是相对于数据区起始位置的偏移）
int fs::ewrite(struct dirent *entry, int user_src, uint64 src, uint off, uint n) {
    if (off > entry->file_size || off + n < off || (uint64)off + n > 0xffffffff || (entry->attribute & ATTR_READ_ONLY)) { return -1; }
    if (entry->attribute & ATTR_LINK){
        struct link li;
        rw_clus(entry->first_clus, 0, 0, (uint64)&li, 0, 36);
        entry->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        entry->attribute = li.de.sne.attr;
    }
    if (entry->first_clus == 0) {   // so file_size if 0 too, which requests off == 0
        entry->cur_clus = entry->first_clus = alloc_clus(entry->dev);
        entry->clus_cnt = 0;
        entry->dirty = 1;
    }
    uint tot, m;
    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        reloc_clus(entry, off, 1);
        m = fat.byts_per_clus - off % fat.byts_per_clus;
        if (n - tot < m) { m = n - tot; }
        if (rw_clus(entry->cur_clus, 1, user_src, src, off % fat.byts_per_clus, m) != m) { break; }
    }
    if(n > 0) {
        if(off > entry->file_size) {
            entry->file_size = off;
            entry->dirty = 1;
        }
    }
    return tot;
}
// trim ' ' in the head and tail, '.' in head, and test legality
// 去除开头和结尾的' '以及开头的'.'  对于非法名字，返回0
char *fs::formatname(char *name) {
    static char illegal[] = { '\"', '*', '/', ':', '<', '>', '?', '\\', '|', 0 };
    char *p;
    while (*name == ' ' || *name == '.') { name++; }
    for (p = name; *p; p++) {
        char c = *p;
        if (c < 0x20 || strchr(illegal, c)) { return 0; }
    }
    while (p-- > name) {
        if (*p != ' ') {
            p[1] = '\0';
            break;
        }
    }
    return name;
}
/*
    Generate an on disk format entry and write to the disk. Caller must hold dp->lock
    @param   dp          the directory
    @param   ep          entry to write on disk
    @param   off         offset int the dp, should be calculated via dirlookup before calling this
*/
// 在dp中生成一个目录并写入磁盘，如果off==0，生成'.'目录，如果0<off<=32，生成'..'目录，如果off>32，生成长名目录，长名保存自ep中，将ep->first_clus设为目录的起始簇
void fs::emake(struct dirent *dp, struct dirent *ep, uint off) {
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("emake: not dir"); }
    if (off % sizeof(union dentry)) { panic("emake: not aligned"); }
    union dentry de;
    memset(&de, 0, sizeof(de));
    if (off <= 32) {  // 短名
        if (off == 0) { strncpy(de.sne.name, ".          ", sizeof(de.sne.name)); }
        else { strncpy(de.sne.name, "..         ", sizeof(de.sne.name)); }
        de.sne.attr = ATTR_DIRECTORY;
        de.sne.fst_clus_hi = (uint16)(ep->first_clus >> 16);        // first clus high 16 bits
        de.sne.fst_clus_lo = (uint16)(ep->first_clus & 0xffff);       // low 16 bits
        de.sne.file_size = 0;                                       // filesize is updated in eupdate()
        off = reloc_clus(dp, off, 1);
        rw_clus(dp->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    }
    else {  // 长名
        int entcnt = (strlen(ep->filename) + CHAR_LONG_NAME - 1) / CHAR_LONG_NAME;   // count of l-n-entries, rounds up
        char shortname[CHAR_SHORT_NAME + 1];
        memset(shortname, 0, sizeof(shortname));
        generate_shortname(shortname, ep->filename);
        de.lne.checksum = cal_checksum((uchar *)shortname);
        de.lne.attr = ATTR_LONG_NAME;
        for (int i = entcnt; i > 0; i--) {
            if ((de.lne.order = i) == entcnt) { de.lne.order |= LAST_LONG_ENTRY; }
            char *p = ep->filename + (i - 1) * CHAR_LONG_NAME;
            uint8 *w = (uint8 *)de.lne.name1;
            int end = 0;
            for (int j = 1; j <= CHAR_LONG_NAME; j++) {
                if (end) {
                    *w++ = 0xff;            // on k210, unaligned reading is illegal
                    *w++ = 0xff;
                }
                else { 
                    if ((*w++ = *p++) == 0) { end = 1; }
                    *w++ = 0;
                }
                switch (j) {
                    case 5:     w = (uint8 *)de.lne.name2; break;
                    case 11:    w = (uint8 *)de.lne.name3; break;
                }
            }
            uint off2 = reloc_clus(dp, off, 1);
            rw_clus(dp->cur_clus, 1, 0, (uint64)&de, off2, sizeof(de));
            off += sizeof(de);
        }
        memset(&de, 0, sizeof(de));
        strncpy(de.sne.name, shortname, sizeof(de.sne.name));
        de.sne.attr = ep->attribute;
        de.sne.fst_clus_hi = (uint16)(ep->first_clus >> 16);      // first clus high 16 bits
        de.sne.fst_clus_lo = (uint16)(ep->first_clus & 0xffff);     // low 16 bits
        de.sne.file_size = ep->file_size;                         // filesize is updated in eupdate()
        off = reloc_clus(dp, off, 1);
        rw_clus(dp->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    }
}
/*
    Allocate an entry on disk. Caller must hold dp->lock.
*/
// 在dp目录下创建一个文件/目录，返回创建的文件/目录
struct dirent *fs::ealloc(struct dirent *dp, char *name, int attr) {
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("ealloc not dir"); }
    if (dp->valid != 1 || !(name = formatname(name))) { return NULL; } // detect illegal character
    struct dirent *ep;
    uint off = 0;
    if ((ep = dirlookup(dp, name, &off)) != 0) { return ep; } // entry exists
    ep = eget(dp, name);
    elock(ep);
    ep->attribute = attr;
    ep->file_size = 0;
    ep->first_clus = 0;
    ep->parent = edup(dp);
    ep->off = off;
    ep->clus_cnt = 0;
    ep->cur_clus = 0;
    ep->dirty = 0;
    strncpy(ep->filename, name, FAT32_MAX_FILENAME);
    ep->filename[FAT32_MAX_FILENAME] = '\0';
    if (attr == ATTR_DIRECTORY) {    // generate "." and ".." for ep
        ep->attribute |= ATTR_DIRECTORY;
        ep->cur_clus = ep->first_clus = alloc_clus(dp->dev);
        emake(ep, ep, 0);
        emake(ep, dp, 32);
    } else {
        ep->attribute |= ATTR_ARCHIVE;
    }
    emake(dp, ep, off);
    ep->valid = 1;
    eunlock(ep);
    return ep;
}
// entry引用数加一
struct dirent *fs::edup(struct dirent *entry) {
    if (entry != 0) {
        // acquire(&ecache.lock);
        entry->ref++;
        // release(&ecache.lock);
    }
    return entry;
}
/*
    Only update filesize and first cluster in this case.
    caller must hold entry->parent->lock
*/
// 在entry的父目录中更新entry的目录项
void fs::eupdate(struct dirent *entry) {
    if (!entry->dirty || entry->valid != 1) { return; }
    uint entcnt = 0;
    uint32 off = reloc_clus(entry->parent, entry->off, 0);
    rw_clus(entry->parent->cur_clus, 0, 0, (uint64) &entcnt, off, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    off = reloc_clus(entry->parent, entry->off + (entcnt << 5), 0);
    union dentry de;
    rw_clus(entry->parent->cur_clus, 0, 0, (uint64)&de, off, sizeof(de));
    de.sne.fst_clus_hi = (uint16)(entry->first_clus >> 16);
    de.sne.fst_clus_lo = (uint16)(entry->first_clus & 0xffff);
    de.sne.file_size = entry->file_size;
    rw_clus(entry->parent->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    entry->dirty = 0;
}
/*
    caller must hold entry->lock
    caller must hold entry->parent->lock
    remove the entry in its parent directory
*/
// 将entry从它的父目录中移除，被移除后entry的valid被置为-1
void fs::eremove(struct dirent *entry) {
    if (entry->valid != 1) { return; }
    uint entcnt = 0;
    uint32 off = entry->off;
    uint32 off2 = reloc_clus(entry->parent, off, 0);
    rw_clus(entry->parent->cur_clus, 0, 0, (uint64) &entcnt, off2, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    uint8 flag = EMPTY_ENTRY;
    for (int i = 0; i <= entcnt; i++) {
      rw_clus(entry->parent->cur_clus, 1, 0, (uint64) &flag, off2, 1);
      off += 32;
      off2 = reloc_clus(entry->parent, off, 0);
    }
    entry->valid = -1;
}
/*
    truncate a file
    caller must hold entry->lock
*/
// 在数据区清空文件/目录
void fs::etrunc(struct dirent *entry) {
    if(!(entry->attribute & ATTR_LINK)){
        for (uint32 clus = entry->first_clus; clus >= 2 && clus < FAT32_EOC; ) {
            uint32 next = read_fat(clus);
            free_clus(clus);
            clus = next;
        }
    }
    entry->file_size = 0;
    entry->first_clus = 0;
    entry->dirty = 1;
}
// 请求睡眠锁，要求引用数大于0
void fs::elock(struct dirent *entry) {
    if (entry == 0 || entry->ref < 1) { panic("elock"); }
    // acquiresleep(&entry->lock);
}
// 释放睡眠锁
void fs::eunlock(struct dirent *entry) {
    // if (entry == 0 || !holdingsleep(&entry->lock) || entry->ref < 1) { panic("eunlock"); }
    if (entry == 0 || entry->ref < 1) { panic("eunlock"); }
    // releasesleep(&entry->lock);
}
// 将entry引用数减少1，如果entry的引用数减少为0，则将entry放置缓冲区最前面，并执行eput(entry->parent)
void fs::eput(struct dirent *entry) {
    // acquire(&ecache.lock);
    if (entry != &root && entry->valid != 0 && entry->ref == 1) {
        // ref == 1 means no other process can have entry locked,
        // so this acquiresleep() won't block (or deadlock).
        // acquiresleep(&entry->lock);
        entry->next->prev = entry->prev;
        entry->prev->next = entry->next;
        entry->next = root.next;
        entry->prev = &root;
        root.next->prev = entry;
        root.next = entry;
        // release(&ecache.lock);
        if (entry->valid == -1) {       // this means some one has called eremove()
            etrunc(entry);
        } else {
            elock(entry->parent);
            eupdate(entry);
            eunlock(entry->parent);
        }
        // releasesleep(&entry->lock);
        // Once entry->ref decreases down to 0, we can't guarantee the entry->parent field remains unchanged.
        // Because eget() may take the entry away and write it.
        struct dirent *eparent = entry->parent;
        // acquire(&ecache.lock);
        entry->ref--;
        // release(&ecache.lock);
        if (entry->ref == 0) { eput(eparent); }
        return;
    }
    entry->ref--;
    // release(&ecache.lock);
}
// 将dirent的信息copy到stat中，包括文件名、文件类型、所在设备号、文件大小
void fs::estat(struct dirent *de, struct stat *st) {
    strncpy(st->name, de->filename, STAT_MAX_NAME);
    st->type = (de->attribute & ATTR_DIRECTORY) ? T_DIR : T_FILE;
    st->dev = de->dev;
    st->size = de->file_size;
}
/*
    Read a directory from off, parse the next entry(ies) associated with one file, or find empty entry slots.
    Caller must hold dp->lock.
    @param   dp      the directory
    @param   ep      the struct to be written with info
    @param   off     offset off the directory
    @param   count   to write the count of entries
    @return  -1      meet the end of dir
             0       find empty slots
             1       find a file with all its entries
*/
/*
    在dp目录中找的off后的下一个文件（off = n*32）,文件名放到ep中
    找到返回1，文件被删除返回0，后面没有文件了返回-1
    count记录了向后遍历了几个目录项
*/
int fs::enext(struct dirent *dp, struct dirent *ep, uint off, int *count) {
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("enext not dir"); }
    if (ep->valid) { panic("enext ep valid"); }
    if (off % 32) { panic("enext not align"); }
    if (dp->valid != 1) { return -1; }
    if (dp->attribute & ATTR_LINK){
        struct link li;
        rw_clus(dp->first_clus, 0, 0, (uint64)&li, 0, 36);
        dp->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        dp->attribute = li.de.sne.attr;
    }
    union dentry de;
    int cnt = 0;
    memset(ep->filename, 0, FAT32_MAX_FILENAME + 1);
    for (int off2; (off2 = reloc_clus(dp, off, 0)) != -1; off += 32) {
        if (rw_clus(dp->cur_clus, 0, 0, (uint64)&de, off2, 32) != 32 || de.lne.order == END_OF_ENTRY) { return -1; }
        if (de.lne.order == EMPTY_ENTRY) {
            cnt++;
            continue;
        }
        else if (cnt) {
            *count = cnt;
            return 0;
        }
        if (de.lne.attr == ATTR_LONG_NAME) {
            int lcnt = de.lne.order & ~LAST_LONG_ENTRY;
            if (de.lne.order & LAST_LONG_ENTRY) {
                *count = lcnt + 1;                              // plus the s-n-e;
                count = 0;
            }
            read_entry_name(ep->filename + (lcnt - 1) * CHAR_LONG_NAME, &de);
        }
        else {
            if (count) {
                *count = 1;
                read_entry_name(ep->filename, &de);
            }
            read_entry_info(ep, &de);
            return 1;
        }
    }
    return -1;
}
/*
    Seacher for the entry in a directory and return a structure. Besides, record the offset of
    some continuous empty slots that can fit the length of filename.
    Caller must hold entry->lock.
    @param   dp          entry of a directory file
    @param   filename    target filename
    @param   poff        offset of proper empty entry slots from the beginning of the dir
*/
// 在dp目录中搜索文件名为filename的文件，poff记录了偏移，返回找到的文件
struct dirent *fs::dirlookup(struct dirent *dp, char *filename, uint *poff) {
     if(dp->mount_flag==1) {
        fat.bpb.byts_per_sec=dev_fat[dp->dev].bpb.byts_per_sec;
        fat.bpb.fat_cnt=dev_fat[dp->dev].bpb.fat_cnt;
        fat.bpb.fat_sz=dev_fat[dp->dev].bpb.fat_sz;
        fat.bpb.hidd_sec=dev_fat[dp->dev].bpb.hidd_sec;
        fat.bpb.root_clus=dev_fat[dp->dev].bpb.root_clus;
        fat.bpb.rsvd_sec_cnt=dev_fat[dp->dev].bpb.rsvd_sec_cnt;
        fat.bpb.sec_per_clus=dev_fat[dp->dev].bpb.sec_per_clus;
        fat.bpb.tot_sec=dev_fat[dp->dev].bpb.tot_sec;
        fat.byts_per_clus=dev_fat[dp->dev].byts_per_clus;
        fat.data_clus_cnt=dev_fat[dp->dev].data_clus_cnt;
        fat.data_sec_cnt=dev_fat[dp->dev].data_sec_cnt;
        fat.first_data_sec=dev_fat[dp->dev].data_sec_cnt;
        root=dev_fat[dp->dev].root;
        dp=&root;
    }
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("dirlookup not DIR"); }
    if (dp->attribute & ATTR_LINK){
        struct link li;
        rw_clus(dp->first_clus, 0, 0, (uint64)&li, 0, 36);
        dp->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        dp->attribute = li.de.sne.attr;
    }
    if (strncmp(filename, ".", FAT32_MAX_FILENAME) == 0) { return edup(dp); }
    else if (strncmp(filename, "..", FAT32_MAX_FILENAME) == 0) {
        if (dp == &root) { return edup(&root); }
        return edup(dp->parent);
    }
    if (dp->valid != 1) {
        printf("valid is not 1\n");
        return NULL;
    }
    struct dirent *ep = eget(dp, filename);
    if (ep->valid == 1) { return ep; }                               // ecache hits
    int len = strlen(filename);
    int entcnt = (len + CHAR_LONG_NAME - 1) / CHAR_LONG_NAME + 1;   // count of l-n-entries, rounds up. plus s-n-e
    int count = 0; 
    int type;
    uint off = 0;
    reloc_clus(dp, 0, 0);
    while ((type = enext(dp, ep, off, &count) != -1)) {
        if (type == 0) {
            printf("%s has been deleted\n", ep->filename);
            if (poff && count >= entcnt) {
                *poff = off;
                poff = 0;
            }
        }
        else if (strncmp(filename, ep->filename, FAT32_MAX_FILENAME) == 0) {
            ep->parent = edup(dp);
            ep->off = off;
            ep->valid = 1;
            return ep;
        }
        off += count << 5;
    }
    if (poff) { *poff = off; }
    eput(ep);
    return NULL;
}
// 根据路径寻找目录（进入该目录）
struct dirent *fs::ename(char *path) {
    char name[FAT32_MAX_FILENAME + 1];
    return lookup_path(path, 0, name);
}
// 根据路径寻找目录（不进入该目录）
struct dirent *fs::enameparent(char *path, char *name) { return lookup_path(path, 1, name); }
// 根据路径寻找目录（进入该文件/目录）
struct dirent *fs::ename2(char *path, struct File *f) {
    char name[FAT32_MAX_FILENAME + 1];
    return lookup_path2(path, 0, f, name);
}
// 根据路径寻找目录（不进入该文件/目录）
struct dirent *fs::enameparent2(char *path, char *name, struct File *f) { return lookup_path2(path, 1, f, name); }

int fs::link(char* oldpath, struct File *f1, char* newpath, struct File *f2){
  struct dirent *dp1, *dp2;
  if((dp1 = ename2(oldpath, f1)) == NULL) {
    printf("can't find dir\n");
    return -1;
  }
  struct dirent *parent1;
  parent1 = dp1->parent;
  int off2;
  off2 = reloc_clus(parent1, dp1->off, 0);
  union dentry de;
  if (rw_clus(parent1->cur_clus, 0, 0, (uint64)&de, off2, 32) != 32 || de.lne.order == END_OF_ENTRY) {
    printf("can't read dentry\n");
    return -1;
  }
  struct link li;
  int clus;
  if(!(de.sne.attr & ATTR_LINK)){
    clus = alloc_clus(dp1->dev);
    li.de = de;
    li.link_count = 2;
    if(rw_clus(clus, 1, 0, (uint64)&li, 0, 36) != 36){
        printf("write li wrong\n");
        return -1;
    }
    de.sne.attr = ATTR_DIRECTORY | ATTR_LINK;
    de.sne.fst_clus_hi = (uint16)(clus >> 16);       
    de.sne.fst_clus_lo = (uint16)(clus & 0xffff);
    de.sne.file_size = 36;
    elock(parent1);
    if(rw_clus(parent1->cur_clus, 1, 0, (uint64)&de, off2, 32) != 32){
        printf("write parent1 wrong\n");
        eunlock(parent1);
        return -1;
    }
    eunlock(parent1);
  }
  else {
    clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
    rw_clus(clus, 0, 0, (uint64)&li, 0, 36);
    li.link_count++;
    if(rw_clus(clus, 1, 0, (uint64)&li, 0, 36) != 36){
        printf("write li wrong\n");
        return -1;
    }
  }
  char name[FAT32_MAX_FILENAME + 1];
  if((dp2 = enameparent2(newpath, name, f2)) == NULL){
    printf("can't find dir\n");
    return NULL;
  }
  struct dirent *ep;
  elock(dp2);
  uint off = 0;
  if((ep = dirlookup(dp2, name, &off)) != 0) {
    printf("%s exits",name);
    return -1;
  }
  off = reloc_clus(dp2, off, 1);
  if(rw_clus(dp2->cur_clus, 1, 0, (uint64)&de, off, 32) != 32){
    printf("write de into %s wrong",dp2->filename);
    eunlock(dp2);
    return -1;
  }
  eunlock(dp2);
  return 0;
}
int fs::unlink(char *path, struct File *f) {
  struct dirent *dp;
  if((dp = ename2(path, f)) == NULL) { return -1; }
  struct dirent *parent;
  parent = dp->parent;
  int off;
  off = reloc_clus(parent, dp->off, 0);
  union dentry de;
  if (rw_clus(parent->cur_clus, 0, 0, (uint64)&de, off, 32) != 32 || de.lne.order == END_OF_ENTRY) {
    printf("can't read dentry\n");
    return -1;
  }
  if(de.sne.attr & ATTR_LINK) {
    int clus;
    struct link li;
    clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
    if(rw_clus(clus, 0, 0, (uint64)&li, 0, 36) != 36){
      printf("read li wrong\n");
      return -1;
    }
    if(--li.link_count == 0){
        free_clus(clus);
        de = li.de;
        if(rw_clus(parent->cur_clus, 1, 0, (uint64)&de, off, 32) != 32){
            printf("write de into %s wrong\n",parent->filename);
            return -1;
        }
    }
  }
  return remove2(path, f);
}
// 目录是否为空
// Is the directory dp empty except for "." and ".." ?
int fs::isdirempty(struct dirent *dp) {
  struct dirent ep;
  int count;
  int ret;
  ep.valid = 0;
  ret = enext(dp, &ep, 2 * 32, &count);   // skip the "." and ".."
  return ret == -1;
}
int fs::remove(char *path) {
  char *s = path + strlen(path) - 1;
  while (s >= path && *s == '/') { s--; }
  if (s >= path && *s == '.' && (s == path || *--s == '/')) { return -1; }
  struct dirent *ep;
  if((ep = ename(path)) == NULL){ return -1; }
  elock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !isdirempty(ep)) {
    eunlock(ep);
    eput(ep);
    return -1;
  }
  elock(ep->parent);      // Will this lead to deadlock?
  eremove(ep);
  eunlock(ep->parent);
  eunlock(ep);
  eput(ep);
  return 0;
}
int fs::remove2(char *path, struct File *f) {
  struct dirent *ep;
  if((ep = ename2(path, f)) == NULL){ return -1; }
  elock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !isdirempty(ep)){
    eunlock(ep);
    eput(ep);
    return -1;
  }
  elock(ep->parent);      // Will this lead to deadlock?
  eremove(ep);
  eunlock(ep->parent);
  eunlock(ep);
  eput(ep);
  return 0;
}
int fs::syn_disk(uint64 start,long len) {
    uint i, n;
    // pte_t *pte;
    uint64 pa,va;
    long off;
    struct proc::Process*p=kHartObjs.curtask->getProcess();
    struct dirent *ep=p->mfile.mfile->ep;
    // pagetable_t pagetable = p->pagetable;
    // if(start>p->mfile.baseaddr) { off=p->mfile.off+start-p->mfile.baseaddr; }
    // else { off=p->mfile.off; }
    // va=start;
    // elock(ep);
    // for(i = 0; i < (int)len; i += PGSIZE) {
    //     pte = walk(pagetable, va+i, 0);
    //     if(pte == 0) { return 0; }
    //     if((*pte & PTE_V) == 0) { return 0; }
    //     if((*pte & PTE_U) == 0) { return 0; }
    //     if((*pte & PTE_D) == 0) { continue; }
    //     pa = PTE2PA(*pte);
    //     if(pa == NULL) { panic("start_map: address should exist"); }
    //     if(len - i < PGSIZE) { n = len - i; }
    //     else { n = PGSIZE; }
    //     printf("write\n");
    //     if(ewrite(ep, 0, (uint64)pa, off+i, n) != n) { return -1; }
    // }
    // eunlock(ep);
    return 1;
}
int fs::do_mount(struct dirent*mountpoint,struct dirent*dev) {
    while(dev_fat[mount_num].vaild!=0) {
        mount_num++;
        mount_num=mount_num%8;
    }
    struct buf *b = bread(dev->dev, 0);
    if (strncmp((char const*)(b->data + 82), "FAT32", 5)) { panic("not FAT32 volume"); }
    memmove(&dev_fat[mount_num].bpb.byts_per_sec, b->data + 11, 2);            // avoid misaligned load on k210
    dev_fat[mount_num].bpb.sec_per_clus = *(b->data + 13);  
    dev_fat[mount_num].bpb.rsvd_sec_cnt = *(uint16 *)(b->data + 14);
    dev_fat[mount_num].bpb.fat_cnt = *(b->data + 16);
    dev_fat[mount_num].bpb.hidd_sec = *(uint32 *)(b->data + 28);
    dev_fat[mount_num].bpb.tot_sec = *(uint32 *)(b->data + 32);
    dev_fat[mount_num].bpb.fat_sz = *(uint32 *)(b->data + 36);
    dev_fat[mount_num].bpb.root_clus = *(uint32 *)(b->data + 44);
    dev_fat[mount_num].first_data_sec = fat.bpb.rsvd_sec_cnt + fat.bpb.fat_cnt * fat.bpb.fat_sz;
    dev_fat[mount_num].data_sec_cnt = fat.bpb.tot_sec - fat.first_data_sec;
    dev_fat[mount_num].data_clus_cnt = fat.data_sec_cnt / fat.bpb.sec_per_clus;
    dev_fat[mount_num].byts_per_clus = fat.bpb.sec_per_clus * fat.bpb.byts_per_sec;
    brelse(b);
    // make sure that byts_per_sec has the same value with BSIZE 
    if (BSIZE != dev_fat[mount_num].bpb.byts_per_sec) { panic("byts_per_sec != BSIZE"); }
    // initlock(&ecache.lock, "ecache");
    memset(&dev_fat[mount_num].root, 0, sizeof(dev_fat[mount_num].root));
    // initsleeplock(&root.lock, "entry");
    dev_fat[mount_num]. root.attribute = (ATTR_DIRECTORY | ATTR_SYSTEM); 
    dev_fat[mount_num].root.first_clus =dev_fat[mount_num]. root.cur_clus = dev_fat[mount_num].bpb.root_clus;
    dev_fat[mount_num].root.valid = 1; 
    dev_fat[mount_num].root.prev = &dev_fat[mount_num].root;
    dev_fat[mount_num].root.next = &dev_fat[mount_num].root;
    dev_fat[mount_num].root.filename[0] = '/';
    dev_fat[mount_num].root.filename[1] = '\0';
    dev_fat[mount_num].mount_mode=1;
    mountpoint->mount_flag=1;
    mountpoint->dev=mount_num;
    return 0;
}
int fs::do_umount(struct dirent*mountpoint) {
   mountpoint->mount_flag=0;
   memset(&dev_fat[mountpoint->dev],0,sizeof(dev_fat[0]));
   mountpoint->dev=0;
   return 0;
}