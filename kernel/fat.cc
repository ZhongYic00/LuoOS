#include "fat.hh"
#include "buf.h"
#include "kernel.hh"

namespace fs { FileSystem dev_fat[8]; }  //挂载设备集合
using namespace fs;
// #define moduleLevel LogLevel::trace

///////////////////FAT////////////////////
static SuperBlock fat;
static struct entry_cache {
    // struct spinlock lock;
    DirEnt entries[ENTRY_CACHE_NUM];
} ecache; // 目录缓冲区
static DirEnt root; // 根目录
int mount_num=0; //表示寻找在挂载集合的下标

int bufCopyOut(int user_dst, uint64 dst, void *src, uint64 len) {
    if(user_dst) { kHartObjs.curtask->getProcess()->vmar.copyout(dst, klib::ByteArray((uint8_t*)src, len)); }
    else { memmove((void*)dst, src, len); }
    return 0;
}
int bufCopyIn(void *dst, int user_src, uint64 src, uint64 len) {
    if(user_src) { memmove(dst, (const void*)(kHartObjs.curtask->getProcess()->vmar.copyin(src, len).buff), len); }
    else { memmove(dst, (void*)src, len); }
    return 0;
}
// @param   cluster   cluster number starts from 2, which means no 0 and 1
// 根据簇号得到起始扇区号
static inline uint32 firstSec(uint32 cluster) { return ((cluster - 2) * fat.rSPC()) + fat.rFDS(); }
/*
    For the given number of a data cluster, return the number of the sector in a FAT table.
    @param   cluster     number of a data cluster
    @param   fat_num     number of FAT table from 1, shouldn't be larger than bpb::fat_cnt
*/
// 根据簇号得到在第fat_num个FAT表中的扇区号
static inline uint32 numthSec(uint32 cluster, uint8 fat_num) {
    return fat.rRSC() + (cluster << 2) / fat.rBPS() + fat.rFS() * (fat_num - 1);
}
/*
    For the given number of a data cluster, return the offest in the corresponding sector in a FAT table.
    @param   cluster   number of a data cluster
*/
// 根据簇号得到在FAT中的扇区偏移
static inline uint32 secOffset(uint32 cluster) { return (cluster << 2) % fat.rBPS(); }
/*
    Read the FAT table content corresponded to the given cluster number.
    @param   cluster     the number of cluster which you want to read its content in FAT table
*/
// 根据簇号读取fat1中的信息
static uint32 fatRead(uint32 cluster) {
    if (cluster >= FAT32_EOC) {
        return cluster;
    }
    if (cluster > fat.rDCC() + 1) {     // because cluster number starts at 2, not 0
        return 0;
    }
    uint32 fat_sec = numthSec(cluster, 1);
    // here should be a cache layer for FAT table, but not implemented yet.
    struct buf *b = bread(0, fat_sec);
    uint32 next_clus = *(uint32 *)(b->data + secOffset(cluster));
    brelse(b);
    return next_clus;
}
/*
    Write the FAT region content corresponded to the given cluster number.
    @param   cluster     the number of cluster to write its content in FAT table
    @param   content     the content which should be the next cluster number of FAT end of chain flag
*/
// 根据簇号将内容写入fat1，return 0 成功 1 失败
static int fatWrite(uint32 cluster, uint32 content) {
    if (cluster > fat.rDCC() + 1) {
        return -1;
    }
    uint32 fat_sec = numthSec(cluster, 1);
    struct buf *b = bread(0, fat_sec);
    uint off = secOffset(cluster);
    *(uint32 *)(b->data + off) = content;
    bwrite(b);
    brelse(b);
    return 0;
}
// 根据簇号将该簇清空
static void clearClus(uint32 cluster) {
    uint32 sec = firstSec(cluster);
    struct buf *b;
    for (int i = 0; i < fat.rSPC(); i++) {
        b = bread(0, sec++);
        memset(b->data, 0, BSIZE);
        bwrite(b);
        brelse(b);
    }
}
// 分配空闲簇，返回分配的簇号
static uint32 allocClus(uint8 dev) {
    // should we keep a free cluster list? instead of searching fat every time.
    struct buf *b;
    uint32 sec = fat.rRSC();
    uint32 const ent_per_sec = fat.rBPS() / sizeof(uint32);
    for (uint32 i = 0; i < fat.rFS(); i++, sec++) {
        b = bread(dev, sec);
        for (uint32 j = 0; j < ent_per_sec; j++) {
            if (((uint32 *)(b->data))[j] == 0) {
                ((uint32 *)(b->data))[j] = FAT32_EOC + 7;
                bwrite(b);
                brelse(b);
                uint32 clus = i * ent_per_sec + j;
                clearClus(clus);
                return clus;
            }
        }
        brelse(b);
    }
    panic("no clusters");
}
// 释放簇
static void freeClus(uint32 cluster) { fatWrite(cluster, 0); }
/* 
    当write为1：将首地址为data、长为n的数据写入簇号为cluster、偏移为off处
    当write为0：将簇号为cluster、偏移为off处、长为n的数据读到data
    返回写入数据的长度
*/
static uint rwClus(uint32 cluster, bool write, bool user, uint64 data, uint off, uint n) {
    if (off + n > fat.rBPC()) { panic("offset out of range"); }
    uint tot, m;
    struct buf *bp;
    uint sec = firstSec(cluster) + off / fat.rBPS();
    off = off % fat.rBPS();
    int bad = 0;
    for (tot = 0; tot < n; tot += m, off += m, data += m, sec++) {
        bp = bread(0, sec);
        m = BSIZE - off % BSIZE;
        if (n - tot < m) { m = n - tot; }
        if (write) {
            if ((bad = bufCopyIn(bp->data + (off % BSIZE), user, data, m)) != -1) { bwrite(bp); }
        }
        else { bad = bufCopyOut(user, data, bp->data + (off % BSIZE), m); }
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
static int relocClus(DirEnt *entry, uint off, int alloc) {
    int clus_num = off / fat.rBPC();
    while (clus_num > entry->clus_cnt) {
        int clus = fatRead(entry->cur_clus);
        if (clus >= FAT32_EOC) {
            if (alloc) {
                clus = allocClus(entry->dev);
                fatWrite(entry->cur_clus, clus);
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
            entry->cur_clus = fatRead(entry->cur_clus);
            if (entry->cur_clus >= FAT32_EOC) {
                panic("relocClus");
            }
            entry->clus_cnt++;
        }
    }
    return off % fat.rBPC();
}
/*
    Returns a DirEnt struct. If name is given, check ecache. It is difficult to cache entries
    by their whole path. But when parsing a path, we open all the directories through it, 
    which forms a linked list from the final file to the root. Thus, we use the "parent" pointer 
    to recognize whether an entry with the "name" as given is really the file we want in the right path.
    Should never get root by entHit, it's easy to understand.
*/
// 在目录缓冲区中根据parent和name寻找目录，如果没有找到则将其放到缓冲区，并将vaild设为0（如果缓冲区有空余的话）（没有置换算法）
static DirEnt *entHit(DirEnt *parent, const char *name) {
    DirEnt *ep;
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
            ep->dirty = false;
            // release(&ecache.lock);
            return ep;
        }
    }
    panic("entHit: insufficient ecache");
    return 0;
}
// 将name转化为短名（小写字母转大写，非法字符变成'_'）
static void genShortName(char *shortname, char *name) {
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
            if (strchr(illegal, c) != nullptr) { c = '_'; }
        }
        shortname[i++] = c;
    }
    while (i < CHAR_SHORT_NAME) { shortname[i++] = ' '; }
}
// 根据shortname计算校验和
uint8 calCheckSum(uchar* shortname) {
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
static void readEntName(char *buffer, union Ent *d) {
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
static void readEntInfo(DirEnt *entry, union Ent *d) {
    entry->attribute = d->sne.attr;
    entry->first_clus = ((uint32)d->sne.fst_clus_hi << 16) | d->sne.fst_clus_lo;
    entry->file_size = d->sne.file_size;
    entry->cur_clus = entry->first_clus;
    entry->clus_cnt = 0;
}
// 对于给定的path，读取路径的开头文件名，返回去掉了开头文件名的路径
static char *readNextElem(char *path, char *name) {
    // 去掉第一个文件名前（即路径开头）的'/'
    while (*path == '/') { path++; }
    if (*path == 0) { return nullptr; }
    // 读取第一个文件名
    char *s = path;
    while (*path != '/' && *path != 0) { path++; }
    // 文件名长度
    int len = path - s;
    if (len > FAT32_MAX_FILENAME) { len = FAT32_MAX_FILENAME; }
    // 文件名存进name里
    name[len] = 0;
    memmove(name, s, len);
    // 去掉第二个文件名前的'/'
    while (*path == '/') { path++; }
    // 返回去掉第一个文件名后的路径
    return path;
}
/*
    FAT32 version of namex in xv6's original file system.
    根据路径找到目录，name记录了最后找到的目录名
    对于/a/b/c/d
    1. 如果parent=1，返回d
    2. 如果parent=0，返回d/
*/
static DirEnt *pathLookUp(char *path, int parent, char *name) {
    DirEnt *entry, *next;
    if (*path == '/') { entry = entDup(&root); }
    else if (*path != '\0') { entry = entDup(kHartObjs.curtask->getProcess()->cwd); }
    else { return nullptr; }
    while ((path = readNextElem(path, name)) != 0) {
        entLock(entry);
        if (!(entry->attribute & ATTR_DIRECTORY)) {
            entUnlock(entry);
            entRelse(entry);
            return nullptr;
        }
        if (parent && *path == '\0') {
            entUnlock(entry);
            return entry;
        }
        if ((next = dirLookUp(entry, name, 0)) == 0) {
            entUnlock(entry);
            entRelse(entry);
            return nullptr;
        }
        entUnlock(entry);
        entRelse(entry);
        entry = next;
    }
    if (parent) {
        entRelse(entry);
        return nullptr;
    }
    return entry;
}
static DirEnt *pathLookUpAt(char *path, int parent, SharedPtr<File> f ,char *name) {
    DirEnt *entry, *next;
    if (*path == '\0') {
        printf("nothing in path\n");
        return nullptr;
    }
    // 增加当前目录（或指定目录）的引用计数
    else if(*path == '/') { entry = entDup(&root); }
    else if(f == nullptr) { entry = entDup(kHartObjs.curtask->getProcess()->cwd); }
    else { entry = entDup(f->obj.ep); }
    // 沿着路径依次搜索
    while ((path = readNextElem(path, name)) != 0) { // 读取一个文件名到name，并将其从path中去掉
        entLock(entry);
        // 当前搜索“目录”（name所属的目录）并非目录
        if (!(entry->attribute & ATTR_DIRECTORY)) {
            printf("not dir\n");
            entUnlock(entry);
            entRelse(entry);
            return nullptr;
        }
        // name是路径中的最后一个文件名
        if (parent && *path == '\0') {
            entUnlock(entry);
            return entry;
        }
        if ((next = dirLookUp(entry, name, 0)) == 0) {
            // printf("can't find %s in %s\n", name, entry->filename);
            entUnlock(entry);
            entRelse(entry);
            return nullptr;
        }
        entUnlock(entry);
        entRelse(entry);
        entry = next;
    }
    if (parent) {
        printf("path is wrong\n");
        entRelse(entry);
        return nullptr;
    }
    return entry;
}

/////////////////fs.hh中定义的函数///////////////////////////
int fs::fat32Init() {
    struct buf *b = bread(0, 0);
    if (strncmp((char const*)(b->data + 82), "FAT32", 5)) { panic("not FAT32 volume"); }
    fat = SuperBlock(*(uint16*)(b->data+11), *(uint8*)(b->data+13), *(uint16*)(b->data+14), *(uint8*)(b->data+16), *(uint32*)(b->data+28), *(uint32*)(b->data+32), *(uint32*)(b->data+36), *(uint32*)(b->data+44));
    brelse(b);
    // make sure that byts_per_sec has the same value with BSIZE 
    if (BSIZE != fat.rBPS()) { panic("byts_per_sec != BSIZE"); }
    // initlock(&ecache.lock, "ecache");
    // root = { {'/','\0'}, ATTR_DIRECTORY|ATTR_SYSTEM, fat.rRC(), 0, fat.rRC(), 0, 0, 0, true, 0, 0, nullptr, root, root, 0 };
    memset(&root, 0, sizeof(root));
    // initsleeplock(&root.lock, "entry");
    root.attribute = (ATTR_DIRECTORY | ATTR_SYSTEM); 
    root.first_clus = root.cur_clus = fat.rRC();
    root.valid = 1; 
    root.prev = &root;
    root.next = &root;
    root.filename[0] = '/';
    root.filename[1] = '\0';
    for(DirEnt *de = ecache.entries; de < ecache.entries + ENTRY_CACHE_NUM; de++) {
        de->dev = 0;
        de->valid = 0;
        de->ref = 0;
        de->dirty = false;
        de->parent = 0;
        de->next = root.next;
        de->prev = &root;
        // initsleeplock(&de->lock, "entry");
        root.next->prev = de;
        root.next = de;
    }
    //将主存的系统存入设备管理
    dev_fat[0] = FileSystem(fat, true, root, 0);
    return 0;
}
uint32 fs::getBytesPerClus() { return fat.rBPC(); }
/* like the original readi, but "reade" is odd, let alone "writee" */
// Caller must hold entry->lock.
// 读取偏移为off，长为n的数据到dst处，并将cur_clus移动到结束处所在的簇（off是相对于数据区起始位置的偏移）
int fs::entRead(DirEnt *entry, int user_dst, uint64 dst, uint off, uint n) {
    if (off > entry->file_size || off + n < off || (entry->attribute & ATTR_DIRECTORY)) { return 0; }
    if (entry->attribute & ATTR_LINK){
        struct Link li;
        rwClus(entry->first_clus, 0, 0, (uint64)&li, 0, 36);
        entry->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        entry->attribute = li.de.sne.attr;
    }
    if (off + n > entry->file_size) { n = entry->file_size - off; }
    uint tot, m;
    for (tot = 0; entry->cur_clus < FAT32_EOC && tot < n; tot += m, off += m, dst += m) {
        relocClus(entry, off, 0);
        m = fat.rBPC() - off % fat.rBPC();
        if (n - tot < m) { m = n - tot; }
        if (rwClus(entry->cur_clus, 0, user_dst, dst, off % fat.rBPC(), m) != m) { break; }
    }
    return tot;
}
// Caller must hold entry->lock.
// 将首地址为src，长为n的数据写入偏移为off处，并将cur_clus移动到结束处所在的簇（off是相对于数据区起始位置的偏移）
int fs::entWrite(DirEnt *entry, int user_src, uint64 src, uint off, uint n) {
    if (off > entry->file_size || off + n < off || (uint64)off + n > 0xffffffff || (entry->attribute & ATTR_READ_ONLY)) { return -1; }
    if (entry->attribute & ATTR_LINK){
        struct Link li;
        rwClus(entry->first_clus, 0, 0, (uint64)&li, 0, 36);
        entry->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        entry->attribute = li.de.sne.attr;
    }
    if (entry->first_clus == 0) {   // so file_size if 0 too, which requests off == 0
        entry->cur_clus = entry->first_clus = allocClus(entry->dev);
        entry->clus_cnt = 0;
        entry->dirty = true;
    }
    uint tot, m;
    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        relocClus(entry, off, 1);
        m = fat.rBPC() - off % fat.rBPC();
        if (n - tot < m) { m = n - tot; }
        if (rwClus(entry->cur_clus, 1, user_src, src, off % fat.rBPC(), m) != m) { break; }
    }
    if(n > 0) {
        if(off > entry->file_size) {
            entry->file_size = off;
            entry->dirty = true;
        }
    }
    return tot;
}
// trim ' ' in the head and tail, '.' in head, and test legality
// 去除开头和结尾的' '以及开头的'.'  对于非法名字，返回0
char *fs::flNameOld(char *name) {
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
    @param   off         offset int the dp, should be calculated via dirLookUp before calling this
*/
// 在dp中生成一个目录并写入磁盘，如果off==0，生成'.'目录，如果0<off<=32，生成'..'目录，如果off>32，生成长名目录，长名保存自ep中，将ep->first_clus设为目录的起始簇
void fs::entSynAt(DirEnt *dp, DirEnt *ep, uint off) {
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("entSynAt: not dir"); }
    if (off % sizeof(union Ent)) { panic("entSynAt: not aligned"); }
    union Ent de;
    memset(&de, 0, sizeof(de));
    if (off <= 32) {  // 短名
        if (off == 0) { strncpy(de.sne.name, ".          ", sizeof(de.sne.name)); }
        else { strncpy(de.sne.name, "..         ", sizeof(de.sne.name)); }
        de.sne.attr = ATTR_DIRECTORY;
        de.sne.fst_clus_hi = (uint16)(ep->first_clus >> 16);        // first clus high 16 bits
        de.sne.fst_clus_lo = (uint16)(ep->first_clus & 0xffff);       // low 16 bits
        de.sne.file_size = 0;                                       // filesize is updated in dirUpdate()
        off = relocClus(dp, off, 1);
        rwClus(dp->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    }
    else {  // 长名
        int entcnt = (strlen(ep->filename) + CHAR_LONG_NAME - 1) / CHAR_LONG_NAME;   // count of l-n-entries, rounds up
        char shortname[CHAR_SHORT_NAME + 1];
        memset(shortname, 0, sizeof(shortname));
        genShortName(shortname, ep->filename);
        de.lne.checksum = calCheckSum((uchar *)shortname);
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
            uint off2 = relocClus(dp, off, 1);
            rwClus(dp->cur_clus, 1, 0, (uint64)&de, off2, sizeof(de));
            off += sizeof(de);
        }
        memset(&de, 0, sizeof(de));
        strncpy(de.sne.name, shortname, sizeof(de.sne.name));
        de.sne.attr = ep->attribute;
        de.sne.fst_clus_hi = (uint16)(ep->first_clus >> 16);      // first clus high 16 bits
        de.sne.fst_clus_lo = (uint16)(ep->first_clus & 0xffff);     // low 16 bits
        de.sne.file_size = ep->file_size;                         // filesize is updated in dirUpdate()
        off = relocClus(dp, off, 1);
        rwClus(dp->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    }
}
/*
    Allocate an entry on disk. Caller must hold dp->lock.
*/
// 在dp目录下创建一个文件/目录，返回创建的文件/目录
DirEnt *fs::entCreateAt(DirEnt *dp, char *name, int attr) {
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("entCreateAt not dir"); }
    if (dp->valid != 1 || !(name = flNameOld(name))) { return nullptr; } // detect illegal character
    DirEnt *ep;
    uint off = 0;
    if ((ep = dirLookUp(dp, name, &off)) != 0) { return ep; } // entry exists
    ep = entHit(dp, name);
    entLock(ep);
    ep->attribute = attr;
    ep->file_size = 0;
    ep->first_clus = 0;
    ep->parent = entDup(dp);
    ep->off = off;
    ep->clus_cnt = 0;
    ep->cur_clus = 0;
    ep->dirty = false;
    strncpy(ep->filename, name, FAT32_MAX_FILENAME);
    ep->filename[FAT32_MAX_FILENAME] = '\0';
    if (attr == ATTR_DIRECTORY) {    // generate "." and ".." for ep
        ep->attribute |= ATTR_DIRECTORY;
        ep->cur_clus = ep->first_clus = allocClus(dp->dev);
        entSynAt(ep, ep, 0);
        entSynAt(ep, dp, 32);
    } else {
        ep->attribute |= ATTR_ARCHIVE;
    }
    entSynAt(dp, ep, off);
    ep->valid = 1;
    entUnlock(ep);
    return ep;
}
// entry引用数加一
DirEnt *fs::entDup(DirEnt *entry) {
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
void fs::dirUpdate(DirEnt *entry) {
    if (!entry->dirty || entry->valid != 1) { return; }
    uint entcnt = 0;
    uint32 off = relocClus(entry->parent, entry->off, 0);
    rwClus(entry->parent->cur_clus, 0, 0, (uint64) &entcnt, off, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    off = relocClus(entry->parent, entry->off + (entcnt << 5), 0);
    union Ent de;
    rwClus(entry->parent->cur_clus, 0, 0, (uint64)&de, off, sizeof(de));
    de.sne.fst_clus_hi = (uint16)(entry->first_clus >> 16);
    de.sne.fst_clus_lo = (uint16)(entry->first_clus & 0xffff);
    de.sne.file_size = entry->file_size;
    rwClus(entry->parent->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    entry->dirty = false;
}
/*
    caller must hold entry->lock
    caller must hold entry->parent->lock
    pathRemove the entry in its parent directory
*/
// 将entry从它的父目录中移除，被移除后entry的valid被置为-1
void fs::entRemove(DirEnt *entry) {
    if (entry->valid != 1) { return; }
    uint entcnt = 0;
    uint32 off = entry->off;
    uint32 off2 = relocClus(entry->parent, off, 0);
    rwClus(entry->parent->cur_clus, 0, 0, (uint64) &entcnt, off2, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    uint8 flag = EMPTY_ENTRY;
    for (int i = 0; i <= entcnt; i++) {
      rwClus(entry->parent->cur_clus, 1, 0, (uint64) &flag, off2, 1);
      off += 32;
      off2 = relocClus(entry->parent, off, 0);
    }
    entry->valid = -1;
}
/*
    truncate a file
    caller must hold entry->lock
*/
// 在数据区清空文件/目录
void fs::entTrunc(DirEnt *entry) {
    if(!(entry->attribute & ATTR_LINK)){
        for (uint32 clus = entry->first_clus; clus >= 2 && clus < FAT32_EOC; ) {
            uint32 next = fatRead(clus);
            freeClus(clus);
            clus = next;
        }
    }
    entry->file_size = 0;
    entry->first_clus = 0;
    entry->dirty = true;
}
// 请求睡眠锁，要求引用数大于0
void fs::entLock(DirEnt *entry) {
    // if (entry == 0 || entry->ref < 1) { panic("entLock"); }
    // acquiresleep(&entry->lock);
}
// 释放睡眠锁
void fs::entUnlock(DirEnt *entry) {
    // if (entry == 0 || !holdingsleep(&entry->lock) || entry->ref < 1) { panic("entUnlock"); }
    // if (entry == 0 || entry->ref < 1) { panic("entUnlock"); }
    // releasesleep(&entry->lock);
}
// 将entry引用数减少1，如果entry的引用数减少为0，则将entry放置缓冲区最前面，并执行eput(entry->parent)
void fs::entRelse(DirEnt *entry) {
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
        if (entry->valid == -1) {       // this means some one has called entRemove()
            entTrunc(entry);
        } else {
            entLock(entry->parent);
            dirUpdate(entry);
            entUnlock(entry->parent);
        }
        // releasesleep(&entry->lock);
        // Once entry->ref decreases down to 0, we can't guarantee the entry->parent field remains unchanged.
        // Because entHit() may take the entry away and write it.
        DirEnt *eparent = entry->parent;
        // acquire(&ecache.lock);
        entry->ref--;
        // release(&ecache.lock);
        if (entry->ref == 0) { entRelse(eparent); }
        return;
    }
    entry->ref--;
    // release(&ecache.lock);
}
// 将dirent的信息copy到stat中，包括文件名、文件类型、所在设备号、文件大小
void fs::entStat(DirEnt *de, struct Stat *st) {
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
int fs::entFindNext(DirEnt *dp, DirEnt *ep, uint off, int *count) {
    Log(trace,"entFindNext(dp=%p,ep=%p)",dp);
    if (!(dp->attribute & ATTR_DIRECTORY)) {
        panic("entFindNext not dir");
    } // 搜索“目录”非目录
    if (ep->valid) { panic("entFindNext ep valid"); } // 存放返回文件的结构无效
    if (off % 32) { panic("entFindNext not align"); } // 未对齐
    if (dp->valid != 1) { return -1; } // 搜索目录无效
    if (dp->attribute & ATTR_LINK){
        struct Link li;
        rwClus(dp->first_clus, 0, 0, (uint64)&li, 0, 36);
        dp->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        dp->attribute = li.de.sne.attr;
    }
    union Ent de;
    int cnt = 0;
    memset(ep->filename, 0, FAT32_MAX_FILENAME + 1);
    // 遍历dp的簇
    for (int off2; (off2 = relocClus(dp, off, 0)) != -1; off += 32) { // off2: 簇内偏移 off: 目录内偏移
        // 没对齐或在非"."和"..."目录的情形下到达结尾
        auto bytes = rwClus(dp->cur_clus, 0, 0, (uint64)&de, off2, 32);
        auto fchar = ((char*)&de)[0];
        auto leorder = de.lne.order;
        static bool first = true;
        if ( bytes!= 32 || leorder==END_OF_ENTRY) { return -1; }
        // 当前目录为空目录
        if (de.lne.order == EMPTY_ENTRY) {
            cnt++;
            continue;
        }
        // 文件已删除
        else if (cnt) {
            *count = cnt;
            return 0;
        }
        // 长目录项
        if (de.lne.attr == ATTR_LONG_NAME) {
            int lcnt = de.lne.order & ~LAST_LONG_ENTRY;
            if (de.lne.order & LAST_LONG_ENTRY) {
                *count = lcnt + 1;                              // plus the s-n-e;
                count = 0;
            }
            readEntName(ep->filename + (lcnt - 1) * CHAR_LONG_NAME, &de);
        }
        // 短目录项
        else {
            if (count) {
                *count = 1;
                readEntName(ep->filename, &de);
            }
            readEntInfo(ep, &de);
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
DirEnt *fs::dirLookUp(DirEnt *dp, const char *filename, uint *poff) {
     if(dp->mount_flag==1) {
        fat = dev_fat[dp->dev];
        root = *(dev_fat[dp->dev].findRoot());
        dp = &root;
    }
    // 当前“目录”非目录
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("dirLookUp not DIR"); }
    if (dp->attribute & ATTR_LINK){
        struct Link li;
        rwClus(dp->first_clus, 0, 0, (uint64)&li, 0, 36);
        dp->first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        dp->attribute = li.de.sne.attr;
    }
    // '.'表示当前目录，则增加当前目录引用计数并返回当前目录
    if (strncmp(filename, ".", FAT32_MAX_FILENAME) == 0) { return entDup(dp); }
    // '..'表示父目录，则增加当前目录的父目录引用计数并返回父目录；如果当前是根目录则同'.'
    else if (strncmp(filename, "..", FAT32_MAX_FILENAME) == 0) {
        if (dp == &root) { return entDup(&root); }
        return entDup(dp->parent);
    }
    // 当前目录无效
    if (dp->valid != 1) {
        printf("valid is not 1\n");
        return nullptr;
    }
    DirEnt *ep = entHit(dp, filename); // 从缓冲区中找
    if (ep->valid == 1) { return ep; }                               // ecache hits
    // 缓冲区找不到则往下执行
    int len = strlen(filename);
    int entcnt = (len + CHAR_LONG_NAME - 1) / CHAR_LONG_NAME + 1;   // count of l-n-entries, rounds up. plus s-n-e
    int count = 0; 
    int type;
    uint off = 0;
    relocClus(dp, 0, 0); // 将当前目录的cur_clus设为0
    while ((type = entFindNext(dp, ep, off, &count) != -1)) { // 每轮从off开始往后搜索
        // 文件已被删除
        if (type == 0) {
            printf("%s has been deleted\n", ep->filename);
            if (poff && count >= entcnt) {
                *poff = off;
                poff = 0;
            }
        }
        // 找到了一个有效文件
        // @todo 不区分大小写？
        else if (strncmpamb(filename, ep->filename, FAT32_MAX_FILENAME) == 0) {
            ep->parent = entDup(dp);
            ep->off = off;
            ep->valid = 1;
            return ep;
        }
        off += count << 5; // off += count*32
    }
    // 没找到有效文件
    if (poff) { *poff = off; } // 从未找到过同名文件（包括已删除的）
    entRelse(ep);
    return nullptr;
}
// 根据路径寻找目录（进入该目录）
DirEnt *fs::entEnter(char *path) {
    char name[FAT32_MAX_FILENAME + 1];
    return pathLookUp(path, 0, name);
}
// 根据路径寻找目录（不进入该目录）
DirEnt *fs::entEnterParent(char *path, char *name) { return pathLookUp(path, 1, name); }
// 根据路径寻找目录（进入该文件/目录）
DirEnt *fs::entEnterFrom(char *path, SharedPtr<File> f) {
    char name[FAT32_MAX_FILENAME + 1];
    return pathLookUpAt(path, 0, f, name);
}
// 根据路径寻找目录（不进入该文件/目录）
DirEnt *fs::entEnterParentAt(char *path, char *name, SharedPtr<File> f) { return pathLookUpAt(path, 1, f, name); }

int fs::entLink(char* oldpath, SharedPtr<File> f1, char* newpath, SharedPtr<File> f2){
  DirEnt *dp1, *dp2;
  if((dp1 = entEnterFrom(oldpath, f1)) == nullptr) {
    printf("can't find dir\n");
    return -1;
  }
  DirEnt *parent1;
  parent1 = dp1->parent;
  int off2;
  off2 = relocClus(parent1, dp1->off, 0);
  union Ent de;
  if (rwClus(parent1->cur_clus, 0, 0, (uint64)&de, off2, 32) != 32 || de.lne.order == END_OF_ENTRY) {
    printf("can't read Ent\n");
    return -1;
  }
  struct Link li;
  int clus;
  if(!(de.sne.attr & ATTR_LINK)){
    clus = allocClus(dp1->dev);
    li.de = de;
    li.link_count = 2;
    if(rwClus(clus, 1, 0, (uint64)&li, 0, 36) != 36){
        printf("write li wrong\n");
        return -1;
    }
    de.sne.attr = ATTR_DIRECTORY | ATTR_LINK;
    de.sne.fst_clus_hi = (uint16)(clus >> 16);       
    de.sne.fst_clus_lo = (uint16)(clus & 0xffff);
    de.sne.file_size = 36;
    entLock(parent1);
    if(rwClus(parent1->cur_clus, 1, 0, (uint64)&de, off2, 32) != 32){
        printf("write parent1 wrong\n");
        entUnlock(parent1);
        return -1;
    }
    entUnlock(parent1);
  }
  else {
    clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
    rwClus(clus, 0, 0, (uint64)&li, 0, 36);
    li.link_count++;
    if(rwClus(clus, 1, 0, (uint64)&li, 0, 36) != 36){
        printf("write li wrong\n");
        return -1;
    }
  }
  char name[FAT32_MAX_FILENAME + 1];
  if((dp2 = entEnterParentAt(newpath, name, f2)) == nullptr){
    printf("can't find dir\n");
    return NULL;
  }
  DirEnt *ep;
  entLock(dp2);
  uint off = 0;
  if((ep = dirLookUp(dp2, name, &off)) != 0) {
    printf("%s exits",name);
    return -1;
  }
  off = relocClus(dp2, off, 1);
  if(rwClus(dp2->cur_clus, 1, 0, (uint64)&de, off, 32) != 32){
    printf("write de into %s wrong",dp2->filename);
    entUnlock(dp2);
    return -1;
  }
  entUnlock(dp2);
  return 0;
}
int fs::entUnlink(char *path, SharedPtr<File> f) {
  DirEnt *dp;
  if((dp = entEnterFrom(path, f)) == nullptr) { return -1; }
  DirEnt *parent;
  parent = dp->parent;
  int off;
  off = relocClus(parent, dp->off, 0);
  union Ent de;
  if (rwClus(parent->cur_clus, 0, 0, (uint64)&de, off, 32) != 32 || de.lne.order == END_OF_ENTRY) {
    printf("can't read Ent\n");
    return -1;
  }
  if(de.sne.attr & ATTR_LINK) {
    int clus;
    struct Link li;
    clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
    if(rwClus(clus, 0, 0, (uint64)&li, 0, 36) != 36){
      printf("read li wrong\n");
      return -1;
    }
    if(--li.link_count == 0){
        freeClus(clus);
        de = li.de;
        if(rwClus(parent->cur_clus, 1, 0, (uint64)&de, off, 32) != 32){
            printf("write de into %s wrong\n",parent->filename);
            return -1;
        }
    }
  }
  return pathRemoveAt(path, f);
}
// 目录是否为空
// Is the directory dp empty except for "." and ".." ?
int fs::dirIsEmpty(DirEnt *dp) {
  DirEnt ep;
  int count;
  int ret;
  ep.valid = 0;
  ret = entFindNext(dp, &ep, 2 * 32, &count);   // skip the "." and ".."
  return ret == -1;
}
int fs::pathRemove(char *path) {
  char *s = path + strlen(path) - 1;
  while (s >= path && *s == '/') { s--; }
  if (s >= path && *s == '.' && (s == path || *--s == '/')) { return -1; }
  DirEnt *ep;
  if((ep = entEnter(path)) == nullptr){ return -1; }
  entLock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !dirIsEmpty(ep)) {
    entUnlock(ep);
    entRelse(ep);
    return -1;
  }
  entLock(ep->parent);      // Will this lead to deadlock?
  entRemove(ep);
  entUnlock(ep->parent);
  entUnlock(ep);
  entRelse(ep);
  return 0;
}
int fs::pathRemoveAt(char *path, SharedPtr<File> f) {
  DirEnt *ep;
  if((ep = entEnterFrom(path, f)) == nullptr){ return -1; }
  entLock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !dirIsEmpty(ep)){
    entUnlock(ep);
    entRelse(ep);
    return -1;
  }
  entLock(ep->parent);      // Will this lead to deadlock?
  entRemove(ep);
  entUnlock(ep->parent);
  entUnlock(ep);
  entRelse(ep);
  return 0;
}
int fs::devMount(DirEnt *mountpoint,DirEnt *dev) {
    while(dev_fat[mount_num].isValid()) {
        mount_num++;
        mount_num=mount_num%8;
    }
    struct buf *b = bread(dev->dev, 0);
    if (strncmp((char const*)(b->data + 82), "FAT32", 5)) { panic("not FAT32 volume"); }
    dev_fat[mount_num] = FileSystem(fat.rRSC()+fat.rFC()*fat.rFS(), fat.rTS()-fat.rFDS(), fat.rDSC()/fat.rSPC(), fat.rSPC()*fat.rBPS(), *(uint16*)(b->data+11), *(uint8*)(b->data+13), *(uint16*)(b->data+14), *(uint8*)(b->data+16), *(uint32*)(b->data+28), *(uint32*)(b->data+32), *(uint32*)(b->data+36), *(uint32*)(b->data+44), true, {0}, 1);
    brelse(b);
    // make sure that byts_per_sec has the same value with BSIZE 
    if (BSIZE != dev_fat[mount_num].rBPS()) { panic("byts_per_sec != BSIZE"); }
    // initlock(&ecache.lock, "ecache");
    DirEnt *nroot = dev_fat[mount_num].getRoot();
    *nroot = { {'/','\0'}, ATTR_DIRECTORY|ATTR_SYSTEM, dev_fat[mount_num].rRC(), 0, dev_fat[mount_num].rRC(), 0, 0, 0, true, 0, 0, nullptr, nroot, nroot, 0 };
    // initsleeplock(&root.lock, "entry");
    mountpoint->mount_flag=1;
    mountpoint->dev=mount_num;
    return 0;
}
int fs::devUnmount(DirEnt *mountpoint) {
    mountpoint->mount_flag=0;
    memset(&dev_fat[mountpoint->dev],0,sizeof(dev_fat[0]));
    mountpoint->dev=0;
    return 0;
}
DirEnt *fs::pathCreate(char *path, short type, int mode) {
    DirEnt *ep, *dp;
    char name[FAT32_MAX_FILENAME + 1];
    if((dp = entEnterParent(path, name)) == nullptr) { return nullptr; }
    if (type == T_DIR) { mode = ATTR_DIRECTORY; }
    else if (mode & O_RDONLY) { mode = ATTR_READ_ONLY; }
    else { mode = 0; }
    entLock(dp);
    if ((ep = entCreateAt(dp, name, mode)) == nullptr) {
        entUnlock(dp);
        entRelse(dp);
        return nullptr;
    }
    if ((type == T_DIR && !(ep->attribute & ATTR_DIRECTORY)) ||
        (type == T_FILE && (ep->attribute & ATTR_DIRECTORY))) {
        entUnlock(dp);
        entRelse(ep);
        entRelse(dp);
        return nullptr;
    }
    entUnlock(dp);
    entRelse(dp);
    entLock(ep);
    return ep;
}
DirEnt *fs::pathCreateAt(char *path, short type, int mode, SharedPtr<File> f) {
    DirEnt *ep, *dp;
    char name[FAT32_MAX_FILENAME + 1];

    if((dp = entEnterParentAt(path, name, f)) == nullptr){
        printf("can't find dir\n");
        return nullptr;
    }
    if (type == T_DIR) { mode = ATTR_DIRECTORY; }
    else if (mode & O_RDONLY) { mode = ATTR_READ_ONLY; }
    else { mode = 0; }
    entLock(dp);
    if ((ep = entCreateAt(dp, name, mode)) == nullptr) {
        entUnlock(dp);
        entRelse(dp);
        return nullptr;
    }
    if ((type == T_DIR && !(ep->attribute & ATTR_DIRECTORY)) ||
        (type == T_FILE && (ep->attribute & ATTR_DIRECTORY))) {
        entUnlock(dp);
        entRelse(ep);
        entRelse(dp);
        return nullptr;
    }
    entUnlock(dp);
    entRelse(dp);
    entLock(ep);
    return ep;
}
void fs::getDStat(DirEnt *de, DStat *st) {
    strncpy(st->d_name, de->filename, STAT_MAX_NAME);
    st->d_type = (de->attribute & ATTR_DIRECTORY) ? S_IFDIR : S_IFREG;
    st->d_ino = de->first_clus;
    st->d_off = 0;
    st->d_reclen = de->file_size;
}
void fs::getKStat(DirEnt *de, KStat *kst) {
    kst->st_dev = de->dev;
    kst->st_ino = de->first_clus;
    kst->st_mode = (de->attribute & ATTR_DIRECTORY) ? S_IFDIR : S_IFREG;
    kst->st_nlink = 1;
    kst->st_uid = 0;
    kst->st_gid = 0;
    kst->st_rdev = 0;
    kst->__pad = 0;
    kst->__pad2 = 0;
    kst->st_size = de->file_size;
    kst->st_blksize = getBytesPerClus();
    kst->st_blocks = (kst->st_size / kst->st_blksize);
    if (kst->st_blocks * kst->st_blksize < kst->st_size) { kst->st_blocks++; }
    kst->st_atime_nsec = 0;
    kst->st_atime_sec = 0;
    kst->st_ctime_nsec = 0;
    kst->st_ctime_sec = 0;
    kst->st_mtime_nsec = 0;
    kst->st_mtime_sec = 0;
    kst->_unused[0] = 0;
    kst->_unused[1] = 0;
}
static string flName(string a_name) {  // @todo 也许是fs的一部分？跟fs关联的非法字符表
    static char illegal[] = { '\"', '*', '/', ':', '<', '>', '?', '\\', '|', 0 };
    size_t beg = 0, i = 0;
    char c;
    for(c = a_name[beg]; c == ' ' || c == '.'; c = a_name[beg]) { ++beg; }
    for (i = beg; c != '\0'; c = a_name[i]) {
        if (c < ' ' || strchr(illegal, c)) { return ""; }
        ++i;
    }
    while (i > beg) {
        --i;
        if (a_name[i] != ' ') { break; }
    }
    return a_name.substr(beg, i+1 - beg);
}
static string getShortName(string a_name) {
    static char illegal[] = { '+', ',', ';', '=', '[', ']', 0 };   // these are legal in l-n-e but not s-n-e
    const char *name = a_name.c_str();
    char shortname[CHAR_SHORT_NAME + 1];
    int i = 0;
    char c;
    const char *p = name;
    for (int j = strlen(name) - 1; j >= 0; j--) {
        if (name[j] == '.') {
            p = name + j; // 最后一个'.'
            break;
        }
    }
    while (i < CHAR_SHORT_NAME && (c = *name++)) {
        if (i == 8 && p) {  
            if (p + 1 < name) { break; }  // no '.'
            else {
                name = p + 1, p = 0;
                continue;
            }
        }
        if (c == ' ') { continue; }
        if (c == '.') {
            if (name > p) {  // last '.'
                memset(shortname + i, ' ', 8 - i);
                i = 8, p = 0;
            }
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c += 'A' - 'a';
        } else {
            if (strchr(illegal, c) != nullptr) { c = '_'; }
        }
        shortname[i++] = c;
    }
    while (i < CHAR_SHORT_NAME) { shortname[i++] = ' '; }
    return shortname;
}
// 根据shortname计算校验和
static uint8 getCheckSum(string shortname) {
    uint8 sum = 0;
    for (int i = CHAR_SHORT_NAME, j = 0; i != 0; --i, ++j) { sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + shortname[j]; }
    return sum;
}
void LNE::readEntName(char *a_buf) const {
    wchar temp[NELEM(name1)];
    memmove(temp, name1, sizeof(temp));
    snstr(a_buf, temp, NELEM(name1));
    a_buf += NELEM(name1);
    snstr(a_buf, name2, NELEM(name2));
    a_buf += NELEM(name2);
    snstr(a_buf, name3, NELEM(name3));
    return;
}
void SNE::readEntName(char *a_buf) const {
    // assert: only "." and ".." will enter this branch
    memset(a_buf, 0, CHAR_SHORT_NAME + 2);  // plus '.' and '\0'
    int i;
    for (i = 0; name[i] != ' ' && i < 8; i++) { a_buf[i] = name[i]; }
    if (name[8] != ' ') { a_buf[i++] = '.'; }
    for (int j = 8; j < CHAR_SHORT_NAME; j++, i++) {
        if (name[j] == ' ') { break; }
        a_buf[i] = name[j];
    }
    return;
}
void Ent::readEntName(char *a_buf) const {
    if (lne.attr == ATTR_LONG_NAME) { lne.readEntName(a_buf); }
    else { sne.readEntName(a_buf); }
    return;
}
DirEnt& DirEnt::operator=(const union Ent& a_ent) {
    attribute = a_ent.sne.attr;
    cur_clus = first_clus = ((uint32)a_ent.sne.fst_clus_hi<<16) | a_ent.sne.fst_clus_lo;
    file_size = a_ent.sne.file_size;
    clus_cnt = 0;
    return *this;
}
DirEnt *DirEnt::entSearch(string a_dirname, uint *a_off) {
    if(mount_flag == 1) {
        fat = dev_fat[dev];
        root = *(dev_fat[dev].findRoot());
        return root.entSearch(a_dirname, a_off);
    }
    // 当前“目录”非目录
    if (!(attribute & ATTR_DIRECTORY)) { panic("dirLookUp not DIR"); }
    if (attribute & ATTR_LINK){
        struct Link li;
        dev_fat[dev].rwClus(first_clus, false, false, (uint64)&li, 0, 36);
        first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        attribute = li.de.sne.attr;
    }
    // '.'表示当前目录，则增加当前目录引用计数并返回当前目录
    if (a_dirname == ".") { return entDup(); }
    // '..'表示父目录，则增加当前目录的父目录引用计数并返回父目录；如果当前是根目录则同'.'
    else if (a_dirname == "..") {
        if (this == &root) { return root.entDup(); }
        else { return parent->entDup(); }
    }
    // 当前目录无效
    if (valid != 1) {
        printf("valid is not 1\n");
        return nullptr;
    }
    // DirEnt *ep = entHit(this, a_dirname.c_str());  // 从缓冲区中找
    DirEnt *ep = eCacheHit(a_dirname);  // 从缓冲区中找
    if (ep->valid == 1) { return ep; }  // ecache hits
    // 缓冲区找不到则往下执行
    size_t len = a_dirname.length();
    int entcnt = (len+CHAR_LONG_NAME-1) / CHAR_LONG_NAME + 1;   // count of l-n-entries, rounds up. plus s-n-e
    int count = 0; 
    int type;
    uint off = 0;
    relocClus(0, false); // 将当前目录的cur_clus设为0
    while ((type = entNext(ep, off, &count) != -1)) { // 每轮从off开始往后搜索
        // 文件已被删除
        if (type == 0) {
            printf("%s has been deleted\n", ep->filename);
            if (a_off != nullptr && count >= entcnt) {
                *a_off = off;
                a_off = nullptr;
            }
        }
        // 找到了一个有效文件
        // @todo 不区分大小写？
        else if (strncmpamb(a_dirname.c_str(), ep->filename, FAT32_MAX_FILENAME) == 0) {
            ep->parent = entDup();
            ep->off = off;
            ep->valid = 1;
            return ep;
        }
        off += count << 5; // off += count*32
    }
    // 没找到有效文件
    if (a_off != nullptr) { *a_off = off; } // 从未找到过同名文件（包括已删除的）
    ep->entRelse();
    return nullptr;
}
int DirEnt::entNext(DirEnt *const a_entry, uint a_off, int *const a_count) {
    if (!(attribute & ATTR_DIRECTORY)) { panic("entFindNext not dir"); }  // 不是目录
    if (a_entry != nullptr && a_entry->valid) { panic("entFindNext ep valid"); }  // 存放结果的结构已被占用
    if (a_off % 32) { panic("entFindNext not align"); } // 未对齐
    if (valid != 1) { return -1; }  // 搜索目录无效
    if (attribute & ATTR_LINK){
        struct Link li;
        dev_fat[dev].rwClus(first_clus, false, false, (uint64)&li, 0, 36);
        first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        attribute = li.de.sne.attr;
    }
    union Ent de;
    int cnt = 0;
    bool islne = false;
    if(a_entry != nullptr) { memset(a_entry->filename, 0, FAT32_MAX_FILENAME + 1); }
    // 遍历dp的簇
    for (int off2; (off2 = relocClus(a_off, false)) != -1; a_off += 32) {  // off2: 簇内偏移 off: 目录内偏移
        // 没对齐或在非"."和".."目录的情形下到达结尾
        auto bytes = dev_fat[dev].rwClus(cur_clus, false, false, (uint64)&de, off2, 32);
        auto fchar = ((char*)&de)[0];
        auto leorder = de.lne.order;
        if (bytes!= 32 || leorder==END_OF_ENTRY) { return -1; }
        // 当前目录为空目录
        if (de.lne.order == EMPTY_ENTRY) {
            cnt++;
            continue;
        }
        // 文件已删除
        else if (cnt != 0) {
            if(a_count != nullptr) { *a_count = cnt; }
            return 0;
        }
        // 长目录项
        if (de.lne.attr == ATTR_LONG_NAME) {
            int lcnt = de.lne.order & ~LAST_LONG_ENTRY;
            if (de.lne.order&LAST_LONG_ENTRY) {
                if(a_count != nullptr) { *a_count = lcnt + 1; }  // plus the s-n-e;
                islne = true;
            }
            if(a_entry != nullptr ) { de.readEntName(a_entry->filename + (lcnt-1) * CHAR_LONG_NAME); }
        }
        // 短目录项
        else {
            if (!islne) {
                if(a_count != nullptr) { *a_count = 1; }
                if(a_entry != nullptr ) { de.readEntName(a_entry->filename); }
            }
            if(a_entry != nullptr ) { *a_entry = de; }
            return 1;
        }
    }
    return -1;
}
int DirEnt::relocClus(uint a_off, bool a_alloc) {
    int clus_num = a_off / dev_fat[dev].rBPC();
    while (clus_num > clus_cnt) {
        int clus = dev_fat[dev].fatRead(cur_clus);
        if (clus >= FAT32_EOC) {
            if (a_alloc) {
                clus = allocClus();
                fatWrite(cur_clus, clus);
            }
            else {
                cur_clus = first_clus;
                clus_cnt = 0;
                return -1;
            }
        }
        cur_clus = clus;
        clus_cnt++;
    }
    if (clus_num < clus_cnt) {
        cur_clus = first_clus;
        clus_cnt = 0;
        while (clus_cnt < clus_num) {
            cur_clus = fatRead(cur_clus);
            if (cur_clus >= FAT32_EOC) { panic("relocClus"); }
            clus_cnt++;
        }
    }
    return a_off % dev_fat[dev].rBPC();
}
const uint32 DirEnt::allocClus() const {  // @todo 应该写成FileSystem的成员？
    // should we keep a free cluster list? instead of searching fat every time.
    struct buf *b;
    uint32 sec = dev_fat[dev].rRSC();
    uint32 const ent_per_sec = dev_fat[dev].rBPS() / sizeof(uint32);
    for (uint32 i = 0; i < dev_fat[dev].rFS(); i++, sec++) {
        b = bread(dev, sec);
        for (uint32 j = 0; j < ent_per_sec; j++) {
            if (((uint32 *)(b->data))[j] == 0) {
                ((uint32 *)(b->data))[j] = FAT32_EOC + 7;
                bwrite(b);
                brelse(b);
                uint32 clus = i * ent_per_sec + j;
                clearClus(clus);
                return clus;
            }
        }
        brelse(b);
    }
    panic("no clusters");
}
DirEnt *DirEnt::entDup() {
    ++ref;
    return this;
}
DirEnt *DirEnt::eCacheHit(string a_name) const {  // @todo 重构ecache，写成ecache的成员
    DirEnt *ep;
    for (ep = root.next; ep != &root; ep = ep->next) {  // LRU algo
        if (ep->valid == 1 && ep->parent == this && strncmpamb(ep->filename, a_name.c_str(), FAT32_MAX_FILENAME) == 0) {  // @todo 不区分大小写？
            if (ep->ref++ == 0) { ep->parent->ref++; }
            return ep;
        }
    }
    for (ep = root.prev; ep != &root; ep = ep->prev) {  // LRU algo
        if (ep->ref == 0) {
            ep->ref = 1;
            ep->dev = dev;
            ep->off = 0;
            ep->valid = 0;
            ep->dirty = false;
            return ep;
        }
    }
    panic("entHit: insufficient ecache");
    return nullptr;
}
void DirEnt::entRelse() {
    // @todo 重构链表操作
    if (this != &root && valid != 0 && ref == 1) {
        // ref == 1 means no other process can have entry locked,
        // so this acquiresleep() won't block (or deadlock).
        next->prev = prev;
        prev->next = next;
        next = root.next;
        prev = &root;
        root.next->prev = this;
        root.next = this;
        if (valid == -1) { entTrunc(); }  // this means some one has called entRemove()
        else { parentUpdate(); }
        // Once entry->ref decreases down to 0, we can't guarantee the entry->parent field remains unchanged.
        // Because entHit() may take the entry away and write it.
        DirEnt *eparent = parent;
        --ref;
        if (ref == 0) { eparent->entRelse(); }
        return;
    }
    --ref;
    return;
}
void DirEnt::entTrunc() {
    if(!(attribute & ATTR_LINK)){
        for (uint32 clus = first_clus; clus >= 2 && clus < FAT32_EOC; ) {
            uint32 next = dev_fat[dev].fatRead(clus);
            freeClus(clus);
            clus = next;
        }
    }
    file_size = 0;
    first_clus = 0;
    dirty = true;
}
void DirEnt::parentUpdate() {
    if (!dirty || valid != 1) { return; }
    uint entcnt = 0;
    uint32 poff = parent->relocClus(off, false);
    dev_fat[dev].rwClus(parent->cur_clus, 0, 0, (uint64) &entcnt, poff, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    poff = parent->relocClus(off + (entcnt<<5), 0);
    union Ent de;
    dev_fat[dev].rwClus(parent->cur_clus, 0, 0, (uint64)&de, poff, sizeof(de));
    de.sne.fst_clus_hi = (uint16)(first_clus >> 16);
    de.sne.fst_clus_lo = (uint16)(first_clus & 0xffff);
    de.sne.file_size = file_size;
    dev_fat[dev].rwClus(parent->cur_clus, 1, 0, (uint64)&de, poff, sizeof(de));
    dirty = false;
}
DirEnt *DirEnt::entCreate(string a_name, int a_attr) {
    if (!(attribute & ATTR_DIRECTORY)) { panic("entCreateAt not dir"); }
    a_name = flName(a_name);
    if (valid != 1 || a_name == "") { return nullptr; }  // detect illegal character
    DirEnt *ep;
    uint off = 0;
    if ((ep = entSearch(a_name, &off)) != 0) { return ep; }  // entry exists
    ep = eCacheHit(a_name);
    ep->attribute = a_attr;
    ep->file_size = 0;
    ep->first_clus = 0;
    ep->parent = entDup();
    ep->off = off;
    ep->clus_cnt = 0;
    ep->cur_clus = 0;
    ep->dirty = false;
    strncpy(ep->filename, a_name.c_str(), FAT32_MAX_FILENAME);
    ep->filename[FAT32_MAX_FILENAME] = '\0';
    if (a_attr == ATTR_DIRECTORY) {    // generate "." and ".." for ep
        ep->attribute |= ATTR_DIRECTORY;
        ep->cur_clus = ep->first_clus = allocClus();
        ep->entCreateOnDisk(ep, 0);
        ep->entCreateOnDisk(this, 32);
    }
    else { ep->attribute |= ATTR_ARCHIVE; }
    entCreateOnDisk(ep, off);
    ep->valid = 1;
    return ep;
}
void DirEnt::entCreateOnDisk(const DirEnt *a_entry, uint a_off) {
    if (!(attribute & ATTR_DIRECTORY)) { panic("entSynAt: not dir"); }
    if (a_off % sizeof(union Ent)) { panic("entSynAt: not aligned"); }
    union Ent de;
    memset(&de, 0, sizeof(de));
    if (a_off <= 32) {  // 短名
        if (a_off == 0) { strncpy(de.sne.name, ".          ", sizeof(de.sne.name)); }
        else { strncpy(de.sne.name, "..         ", sizeof(de.sne.name)); }
        de.sne.attr = ATTR_DIRECTORY;
        de.sne.fst_clus_hi = (uint16)(a_entry->first_clus >> 16);  // first clus high 16 bits
        de.sne.fst_clus_lo = (uint16)(a_entry->first_clus & 0xffff);  // low 16 bits
        de.sne.file_size = 0;  // filesize is updated in dirUpdate()
        a_off = relocClus(a_off, true);
        dev_fat[dev].rwClus(cur_clus, true, false, (uint64)&de, a_off, sizeof(de));
    }
    else {  // 长名
        int entcnt = (strlen(a_entry->filename) + CHAR_LONG_NAME - 1) / CHAR_LONG_NAME;   // count of l-n-entries, rounds up
        string shortname = getShortName(a_entry->filename);
        de.lne.checksum = getCheckSum(shortname);
        de.lne.attr = ATTR_LONG_NAME;
        for (int i = entcnt; i > 0; i--) {
            if ((de.lne.order = i) == entcnt) { de.lne.order |= LAST_LONG_ENTRY; }
            const char *p = a_entry->filename + (i-1) * CHAR_LONG_NAME;
            uint8 *w = (uint8*)de.lne.name1;
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
                    case 5: w = (uint8*)de.lne.name2; break;
                    case 11: w = (uint8*)de.lne.name3; break;
                }
            }
            uint off2 = relocClus(a_off, true);
            dev_fat[dev].rwClus(cur_clus, true, false, (uint64)&de, off2, sizeof(de));
            a_off += sizeof(de);
        }
        memset(&de, 0, sizeof(de));
        strncpy(de.sne.name, shortname.c_str(), sizeof(de.sne.name));
        de.sne.attr = a_entry->attribute;
        de.sne.fst_clus_hi = (uint16)(a_entry->first_clus >> 16);  // first clus high 16 bits
        de.sne.fst_clus_lo = (uint16)(a_entry->first_clus & 0xffff);  // low 16 bits
        de.sne.file_size = a_entry->file_size;  // filesize is updated in dirUpdate()
        a_off = relocClus(a_off, true);
        dev_fat[dev].rwClus(cur_clus, true, false, (uint64)&de, a_off, sizeof(de));
    }
}
int DirEnt::entRead(bool a_usrdst, uint64 a_dst, uint a_off, uint a_len) {
    if (a_off > file_size || a_off + a_len < a_off || (attribute & ATTR_DIRECTORY)) { return 0; }
    if (attribute & ATTR_LINK){
        struct Link li;
        dev_fat[dev].rwClus(first_clus, false, false, (uint64)&li, 0, 36);
        first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        attribute = li.de.sne.attr;
    }
    if (a_off + a_len > file_size) { a_len = file_size - a_off; }
    uint tot, m;
    for (tot = 0; cur_clus < FAT32_EOC && tot < a_len; tot += m, a_off += m, a_dst += m) {
        relocClus(a_off, false);
        m = dev_fat[dev].rBPC() - a_off % dev_fat[dev].rBPC();
        if (a_len - tot < m) { m = a_len - tot; }
        if (dev_fat[dev].rwClus(cur_clus, false, a_usrdst, a_dst, a_off % dev_fat[dev].rBPC(), m) != m) { break; }
    }
    return tot;
}
int DirEnt::entWrite(bool a_usrsrc, uint64 a_src, uint a_off, uint a_len) {
    if (a_off > file_size || a_off + a_len < a_off || (uint64)a_off + a_len > 0xffffffff || (attribute & ATTR_READ_ONLY)) { return -1; }
    if (attribute & ATTR_LINK){
        struct Link li;
        dev_fat[dev].rwClus(first_clus, false, false, (uint64)&li, 0, 36);
        first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        attribute = li.de.sne.attr;
    }
    if (first_clus == 0) {   // so file_size if 0 too, which requests a_off == 0
        cur_clus = first_clus = allocClus();
        clus_cnt = 0;
        dirty = true;
    }
    uint tot, m;
    for (tot = 0; tot < a_len; tot += m, a_off += m, a_src += m) {
        relocClus(a_off, true);
        m = dev_fat[dev].rBPC() - a_off % dev_fat[dev].rBPC();
        if (a_len - tot < m) { m = a_len - tot; }
        if (dev_fat[dev].rwClus(cur_clus, true, a_usrsrc, a_src, a_off % dev_fat[dev].rBPC(), m) != m) { break; }
    }
    if(a_len > 0) {
        if(a_off > file_size) {
            file_size = a_off;
            dirty = true;
        }
    }
    return tot;
}
void DirEnt::entRemove() {
    if (valid != 1) { return; }
    uint entcnt = 0;
    uint32 off1 = off;
    uint32 off2 = parent->relocClus(off1, false);
    dev_fat[parent->dev].rwClus(parent->cur_clus, false, false, (uint64)&entcnt, off2, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    uint8 flag = EMPTY_ENTRY;
    for (int i = 0; i <= entcnt; i++) {
        dev_fat[parent->dev].rwClus(parent->cur_clus, true, false, (uint64)&flag, off2, 1);
        off1 += 32;
        off2 = parent->relocClus(off1, false);
    }
    valid = -1;
    return;
}
SuperBlock::BPB& SuperBlock::BPB::operator=(const BPB& a_bpb) {
    byts_per_sec = a_bpb.byts_per_sec;
    sec_per_clus = a_bpb.sec_per_clus;
    rsvd_sec_cnt = a_bpb.rsvd_sec_cnt;
    fat_cnt = a_bpb.fat_cnt;
    hidd_sec = a_bpb.hidd_sec;
    tot_sec = a_bpb.tot_sec;
    fat_sz = a_bpb.fat_sz;
    root_clus = a_bpb.root_clus;
    return *this;
}
SuperBlock& SuperBlock::operator=(const SuperBlock& a_spblk) {
    first_data_sec = a_spblk.first_data_sec;
    data_sec_cnt = a_spblk.data_sec_cnt;
    data_clus_cnt = a_spblk.data_clus_cnt;
    byts_per_clus = a_spblk.byts_per_clus;
    bpb = a_spblk.bpb;
    return *this;
}
const uint SuperBlock::rwClus(uint32 a_cluster, bool a_iswrite, bool a_usrbuf, uint64 a_buf, uint a_off, uint a_len) const {
    if (a_off + a_len > rBPC()) { panic("offset out of range"); }
    uint tot, m;
    struct buf *bp;
    uint sec = firstSec(a_cluster) + a_off / rBPS();
    a_off = a_off % rBPS();
    int bad = 0;
    for (tot = 0; tot < a_len; tot += m, a_off += m, a_buf += m, sec++) {
        bp = bread(0, sec);  // @todo 设备号？
        m = BSIZE - a_off % BSIZE;
        if (a_len - tot < m) { m = a_len - tot; }
        // @todo 弃用bufCopyIn/Out()
        if (a_iswrite) {
            if ((bad = bufCopyIn(bp->data + (a_off % BSIZE), a_usrbuf, a_buf, m)) != -1) { bwrite(bp); }
        }
        else { bad = bufCopyOut(a_usrbuf, a_buf, bp->data + (a_off % BSIZE), m); }
        brelse(bp);
        if (bad == -1) { break; }
    }
    return tot;
}
const uint32 SuperBlock::fatRead(uint32 a_cluster) const {
    if (a_cluster >= FAT32_EOC) { return a_cluster; }
    if (a_cluster > rDCC() + 1) { return 0; }  // because cluster number starts at 2, not 0
    uint32 fat_sec = numthSec(a_cluster, 1);
    // here should be a cache layer for FAT table, but not implemented yet.
    struct buf *b = bread(0, fat_sec);
    uint32 next_clus = *(uint32*)(b->data + secOffset(a_cluster));
    brelse(b);
    return next_clus;
}
void SuperBlock::clearClus(uint32 a_cluster) const {
    uint32 sec = firstSec(a_cluster);
    struct buf *b;
    for (int i = 0; i < rSPC(); i++) {
        b = bread(0, sec++);
        memset(b->data, 0, BSIZE);
        bwrite(b);
        brelse(b);
    }
    return;
}
const int SuperBlock::fatWrite(uint32 a_cluster, uint32 a_content) const {
    if (a_cluster > rDCC()+1) { return -1; }
    uint32 fat_sec = numthSec(a_cluster, 1);
    struct buf *b = bread(0, fat_sec);
    uint off = secOffset(a_cluster);
    *(uint32*)(b->data + off) = a_content;
    bwrite(b);
    brelse(b);
    return 0;
}
FileSystem& FileSystem::operator=(const FileSystem& a_fs) {
    SuperBlock::operator=(a_fs);
    valid = a_fs.valid;
    root = a_fs.root;
    mount_mode = a_fs.mount_mode;
    return *this;
}
const Path& Path::operator=(const Path& a_path) {
    pathname = a_path.pathname;
    dirname = a_path.dirname;
    return *this;
}
void Path::pathBuild() {
    size_t len = pathname.length();
    if(len > 0) {  // 保证数组长度不为0
        auto ind = new size_t[len][2] { { 0, 0 } };
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
        for(size_t i = 0; i < dirnum; ++i) { dirname[i] = pathname.substr(ind[i][0], ind[i][1]); }
        delete[] ind;
    }
    return;
}
DirEnt *Path::pathSearch(SharedPtr<File> a_file, bool a_parent) const {  // @todo 改成返回File
    DirEnt *entry, *next;
    int dirnum = dirname.size();
    if(pathname.length() < 1) { return nullptr; }  // 空路径
    else if(pathname[0] == '/') { entry = root.entDup(); }  // 绝对路径
    else if(a_file != nullptr) { entry = a_file->obj.ep->entDup(); }  // 相对路径（指定目录）
    else { entry = kHartObjs.curtask->getProcess()->cwd->entDup(); }  // 相对路径（工作目录）
    for(int i = 0; i < dirnum; ++i) {
        if (!(entry->attribute & ATTR_DIRECTORY)) {
            entry->entRelse();
            return nullptr;
        }
        if (a_parent && i == dirnum-1) { return entry; }
        if ((next = entry->entSearch(dirname[i])) == nullptr) {
           entry->entRelse();
            return nullptr;
        }
        entry->entRelse();
        entry = next;
    }
    return entry;
}
DirEnt *Path::pathCreate(short a_type, int a_mode, SharedPtr<File> a_file) const {  // @todo 改成返回File
    DirEnt *ep, *dp;
    if((dp = pathSearch(a_file, true)) == nullptr){
        printf("can't find dir\n");
        return nullptr;
    }
    if (a_type == T_DIR) { a_mode = ATTR_DIRECTORY; }
    else if (a_mode & O_RDONLY) { a_mode = ATTR_READ_ONLY; }
    else { a_mode = 0; }
    if ((ep = dp->entCreate(dirname.back(), a_mode)) == nullptr) {
        dp->entRelse();
        return nullptr;
    }
    if ((a_type==T_DIR && !(ep->attribute&ATTR_DIRECTORY)) || (a_type==T_FILE && (ep->attribute&ATTR_DIRECTORY))) {
        ep->entRelse();
        dp->entRelse();
        return nullptr;
    }
    dp->entRelse();
    return ep;
}
int Path::pathRemove(SharedPtr<File> a_file) const {
    DirEnt *ep = pathSearch(a_file);
    if(ep == nullptr) { return -1; }
    if((ep->attribute & ATTR_DIRECTORY) && !ep->isEmpty()) {
        entRelse(ep);
        return -1;
    }
    ep->entRemove();
    entRelse(ep);
    return 0;
}
int Path::pathLink(SharedPtr<File> a_f1, const Path& a_newpath, SharedPtr<File> a_f2) const {
    DirEnt *dp1 = pathSearch(a_f1);
    if(dp1 == nullptr) {
        printf("can't find dir\n");
        return -1;
    }
    DirEnt *parent1 = dp1->parent;
    int off2 = parent1->relocClus(dp1->off, false);
    union Ent de;
    if (dev_fat[parent1->dev].rwClus(parent1->cur_clus, false, false, (uint64)&de, off2, 32) != 32 || de.lne.order == END_OF_ENTRY) {
        printf("can't read Ent\n");
        return -1;
    }
    struct Link li;
    int clus;
    if(!(de.sne.attr & ATTR_LINK)){
        clus = dp1->allocClus();
        li.de = de;
        li.link_count = 2;
        if(dev_fat[dp1->dev].rwClus(clus, true, false, (uint64)&li, 0, 36) != 36){
            printf("write li wrong\n");
            return -1;
        }
        de.sne.attr = ATTR_DIRECTORY | ATTR_LINK;
        de.sne.fst_clus_hi = (uint16)(clus >> 16);       
        de.sne.fst_clus_lo = (uint16)(clus & 0xffff);
        de.sne.file_size = 36;
        if(dev_fat[parent1->dev].rwClus(parent1->cur_clus, true, false, (uint64)&de, off2, 32) != 32){
            printf("write parent1 wrong\n");
            return -1;
        }
    }
    else {
        clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
        dev_fat[dp1->dev].rwClus(clus, false, false, (uint64)&li, 0, 36);
        li.link_count++;
        if(dev_fat[dp1->dev].rwClus(clus, true, false, (uint64)&li, 0, 36) != 36){
            printf("write li wrong\n");
            return -1;
        }
    }
    DirEnt *dp2 = a_newpath.pathSearch(a_f2);
    if(dp2 == nullptr) {
        printf("can't find dir\n");
        return NULL;
    }
    uint off = 0;
    DirEnt *ep = dp2->entSearch(a_newpath.dirname.back(), &off);
    if(ep != nullptr) {
        printf("%s exits", a_newpath.dirname.back().c_str());
        return -1;
    }
    off = dp2->relocClus(off, true);
    if(dev_fat[dp2->dev].rwClus(dp2->cur_clus, true, false, (uint64)&de, off, 32) != 32){
        printf("write de into %s wrong",dp2->filename);
        return -1;
    }
    return 0;
}
int Path::pathUnlink(SharedPtr<File> a_file) const {
    DirEnt *dp = pathSearch(a_file);
    if(dp == nullptr) { return -1; }
    DirEnt *parent = dp->parent;
    int off = parent->relocClus(dp->off, false);
    union Ent de;
    if (dev_fat[parent->dev].rwClus(parent->cur_clus, false, false, (uint64)&de, off, 32) != 32 || de.lne.order == END_OF_ENTRY) {
        printf("can't read Ent\n");
        return -1;
    }
    if(de.sne.attr & ATTR_LINK) {
        int clus;
        struct Link li;
        clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
        if(dev_fat[dp->dev].rwClus(clus, false, false, (uint64)&li, 0, 36) != 36) {
            printf("read li wrong\n");
            return -1;
        }
        if(--li.link_count == 0){
            freeClus(clus);
            de = li.de;
            if(dev_fat[parent->dev].rwClus(parent->cur_clus, true, false, (uint64)&de, off, 32) != 32){
                printf("write de into %s wrong\n",parent->filename);
                return -1;
            }
        }
    }
    return pathRemove(a_file);
}