#include "fat.hh"
#include "buf.h"
#include "kernel.hh"

using namespace fs;
// #define moduleLevel LogLevel::trace

///////////////////FAT////////////////////
static SuperBlock fat;
static struct entry_cache {
    // struct spinlock lock;
    struct DirEnt entries[ENTRY_CACHE_NUM];
} ecache; // 目录缓冲区
static struct DirEnt root; // 根目录
FileSystem dev_fat[8]; //挂载设备集合
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
static uint rwClus(uint32 cluster, int write, int user, uint64 data, uint off, uint n) {
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
static int relocClus(struct DirEnt *entry, uint off, int alloc) {
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
static struct DirEnt *entHit(struct DirEnt *parent, char *name) {
    struct DirEnt *ep;
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
static void readEntInfo(struct DirEnt *entry, union Ent *d) {
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
static struct DirEnt *pathLookUp(char *path, int parent, char *name) {
    struct DirEnt *entry, *next;
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
static struct DirEnt *pathLookUpAt(char *path, int parent, SharedPtr<File> f ,char *name) {
    struct DirEnt *entry, *next;
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
    for(struct DirEnt *de = ecache.entries; de < ecache.entries + ENTRY_CACHE_NUM; de++) {
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
    dev_fat[0] = FileSystem(fat, true, root, 0);
    return 0;
}
uint32 fs::getBytesPerClus() { return fat.rBPC(); }
/* like the original readi, but "reade" is odd, let alone "writee" */
// Caller must hold entry->lock.
// 读取偏移为off，长为n的数据到dst处，并将cur_clus移动到结束处所在的簇（off是相对于数据区起始位置的偏移）
int fs::entRead(struct DirEnt *entry, int user_dst, uint64 dst, uint off, uint n) {
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
int fs::entWrite(struct DirEnt *entry, int user_src, uint64 src, uint off, uint n) {
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
        entry->dirty = 1;
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
            entry->dirty = 1;
        }
    }
    return tot;
}
// trim ' ' in the head and tail, '.' in head, and test legality
// 去除开头和结尾的' '以及开头的'.'  对于非法名字，返回0
char *fs::flName(char *name) {
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
void fs::entSynAt(struct DirEnt *dp, struct DirEnt *ep, uint off) {
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
struct DirEnt *fs::entCreateAt(struct DirEnt *dp, char *name, int attr) {
    if (!(dp->attribute & ATTR_DIRECTORY)) { panic("entCreateAt not dir"); }
    if (dp->valid != 1 || !(name = flName(name))) { return nullptr; } // detect illegal character
    struct DirEnt *ep;
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
    ep->dirty = 0;
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
struct DirEnt *fs::entDup(struct DirEnt *entry) {
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
void fs::dirUpdate(struct DirEnt *entry) {
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
    entry->dirty = 0;
}
/*
    caller must hold entry->lock
    caller must hold entry->parent->lock
    pathRemove the entry in its parent directory
*/
// 将entry从它的父目录中移除，被移除后entry的valid被置为-1
void fs::entRemove(struct DirEnt *entry) {
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
void fs::entTrunc(struct DirEnt *entry) {
    if(!(entry->attribute & ATTR_LINK)){
        for (uint32 clus = entry->first_clus; clus >= 2 && clus < FAT32_EOC; ) {
            uint32 next = fatRead(clus);
            freeClus(clus);
            clus = next;
        }
    }
    entry->file_size = 0;
    entry->first_clus = 0;
    entry->dirty = 1;
}
// 请求睡眠锁，要求引用数大于0
void fs::entLock(struct DirEnt *entry) {
    // if (entry == 0 || entry->ref < 1) { panic("entLock"); }
    // acquiresleep(&entry->lock);
}
// 释放睡眠锁
void fs::entUnlock(struct DirEnt *entry) {
    // if (entry == 0 || !holdingsleep(&entry->lock) || entry->ref < 1) { panic("entUnlock"); }
    // if (entry == 0 || entry->ref < 1) { panic("entUnlock"); }
    // releasesleep(&entry->lock);
}
// 将entry引用数减少1，如果entry的引用数减少为0，则将entry放置缓冲区最前面，并执行eput(entry->parent)
void fs::entRelse(struct DirEnt *entry) {
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
        struct DirEnt *eparent = entry->parent;
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
void fs::entStat(struct DirEnt *de, struct Stat *st) {
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
int fs::entFindNext(struct DirEnt *dp, struct DirEnt *ep, uint off, int *count) {
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
struct DirEnt *fs::dirLookUp(struct DirEnt *dp, char *filename, uint *poff) {
     if(dp->mount_flag==1) {
        fat = dev_fat[dp->dev].rSpBlk();
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
    struct DirEnt *ep = entHit(dp, filename); // 从缓冲区中找
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
struct DirEnt *fs::entEnter(char *path) {
    char name[FAT32_MAX_FILENAME + 1];
    return pathLookUp(path, 0, name);
}
// 根据路径寻找目录（不进入该目录）
struct DirEnt *fs::entEnterParent(char *path, char *name) { return pathLookUp(path, 1, name); }
// 根据路径寻找目录（进入该文件/目录）
struct DirEnt *fs::entEnterFrom(char *path, SharedPtr<File> f) {
    char name[FAT32_MAX_FILENAME + 1];
    return pathLookUpAt(path, 0, f, name);
}
// 根据路径寻找目录（不进入该文件/目录）
struct DirEnt *fs::entEnterParentAt(char *path, char *name, SharedPtr<File> f) { return pathLookUpAt(path, 1, f, name); }

int fs::entLink(char* oldpath, SharedPtr<File> f1, char* newpath, SharedPtr<File> f2){
  struct DirEnt *dp1, *dp2;
  if((dp1 = entEnterFrom(oldpath, f1)) == nullptr) {
    printf("can't find dir\n");
    return -1;
  }
  struct DirEnt *parent1;
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
  struct DirEnt *ep;
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
  struct DirEnt *dp;
  if((dp = entEnterFrom(path, f)) == nullptr) { return -1; }
  struct DirEnt *parent;
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
int fs::dirIsEmpty(struct DirEnt *dp) {
  struct DirEnt ep;
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
  struct DirEnt *ep;
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
  struct DirEnt *ep;
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
int fs::devMount(struct DirEnt *mountpoint,struct DirEnt *dev) {
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
    struct DirEnt *nroot = dev_fat[mount_num].getRoot();
    *nroot = { {'/','\0'}, ATTR_DIRECTORY|ATTR_SYSTEM, dev_fat[mount_num].rRC(), 0, dev_fat[mount_num].rRC(), 0, 0, 0, true, 0, 0, nullptr, nroot, nroot, 0 };
    // initsleeplock(&root.lock, "entry");
    mountpoint->mount_flag=1;
    mountpoint->dev=mount_num;
    return 0;
}
int fs::devUnmount(struct DirEnt *mountpoint) {
    mountpoint->mount_flag=0;
    memset(&dev_fat[mountpoint->dev],0,sizeof(dev_fat[0]));
    mountpoint->dev=0;
    return 0;
}
struct DirEnt *fs::pathCreate(char *path, short type, int mode) {
    struct DirEnt *ep, *dp;
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
struct DirEnt *fs::pathCreateAt(char *path, short type, int mode, SharedPtr<File> f) {
    struct DirEnt *ep, *dp;
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
void fs::getDStat(struct DirEnt *de, struct DStat *st) {
    strncpy(st->d_name, de->filename, STAT_MAX_NAME);
    st->d_type = (de->attribute & ATTR_DIRECTORY) ? S_IFDIR : S_IFREG;
    st->d_ino = de->first_clus;
    st->d_off = 0;
    st->d_reclen = de->file_size;
}
void fs::getKStat(struct DirEnt *de, struct KStat *kst) {
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