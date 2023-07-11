#include "fat.hh"
#include "kernel.hh"

// namespace fat { FileSystem dev_fat[8]; }  //挂载设备集合
using namespace fat;
using fs::File;
// #define moduleLevel LogLevel::trace

static SuperBlock fat;
static struct entry_cache { DirEnt entries[ENTRY_CACHE_NUM]; } ecache; // 目录缓冲区
static DirEnt root; // 根目录
static int mount_num = 0; //表示寻找在挂载集合的下标
static int bufCopyOut(int user_dst, uint64 dst, void *src, uint64 len) {
    if(user_dst) { kHartObj().curtask->getProcess()->vmar.copyout(dst, klib::ByteArray((uint8_t*)src, len)); }
    else { memmove((void*)dst, src, len); }
    return 0;
}
static int bufCopyIn(void *dst, int user_src, uint64 src, uint64 len) {
    if(user_src) { memmove(dst, (const void*)(kHartObj().curtask->getProcess()->vmar.copyin(src, len).buff), len); }
    else { memmove(dst, (void*)src, len); }
    return 0;
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
static DirEnt *eCacheAlloc(shared_ptr<SuperBlock> a_spblk = nullptr) {
    DirEnt *head = &(ecache.entries[0]);
    for (DirEnt *ep = head->prev; ep != head; ep = ep->prev) {  // LRU algo
        if (ep->ref == 0) {
            ep->ref = 1;
            ep->spblk = a_spblk;
            ep->off = 0;
            ep->valid = 0;
            ep->dirty = false;
            return ep;
        }
    }
    panic("entHit: insufficient ecache");
    return nullptr;
}

int fs::rootFSInit() {  // @todo 重构为entMount + eCacheInit
    auto buf=bcache[{ 0, 0 }];
    DirEnt *root = &(ecache.entries[0]);
    *root = DirEnt("/", ATTR_DIRECTORY|ATTR_SYSTEM, 0, 0, root, root);
    dev_fat[0] = FileSystem(*buf, true, make_shared<DEntry>(root), false);
    // make sure that byts_per_sec has the same value with BlockBuf::blockSize 
    if (BlockBuf::blockSize != dev_fat[0].rBPS()) { panic("byts_per_sec != BlockBuf::blockSize"); }
    root->first_clus = root->cur_clus = dev_fat[0].rRC();
    // DirEnt *root = dev_fat[0].getRoot();
    // *root = { {'/','\0'}, ATTR_DIRECTORY|ATTR_SYSTEM, dev_fat[0].rRC(), 0, dev_fat[0].rRC(), 0, 0, false, 1, 0, 0, nullptr, root, root, 0 };
    for(DirEnt *de = ecache.entries + 1; de < ecache.entries + ENTRY_CACHE_NUM; de++) {  // @todo 重构链表操作
        de->spblk = nullptr;
        de->valid = 0;
        de->ref = 0;
        de->dirty = false;
        de->parent = nullptr;
        de->next = root->next;
        de->prev = root;
        root->next->prev = de;
        root->next = de;
    }
    //将主存的系统存入设备管理
    return 0;
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
DirEnt& DirEnt::operator=(const DirEnt& a_entry) {
    strncpy(filename, a_entry.filename, FAT32_MAX_FILENAME);
    attribute = a_entry.attribute;
    first_clus = a_entry.first_clus;
    file_size = a_entry.file_size;
    cur_clus = a_entry.cur_clus;
    clus_cnt = a_entry.clus_cnt;
    spblk = a_entry.spblk;
    dirty = a_entry.dirty;
    valid = a_entry.valid;
    ref = a_entry.ref;
    off = a_entry.off;
    parent = a_entry.parent;
    next = a_entry.next;
    prev = a_entry.prev;
    mount_flag = a_entry.mount_flag;
    return *this;
}
DirEnt& DirEnt::operator=(const union Ent& a_ent) {
    attribute = a_ent.sne.attr;
    cur_clus = first_clus = ((uint32)a_ent.sne.fst_clus_hi<<16) | a_ent.sne.fst_clus_lo;
    file_size = a_ent.sne.file_size;
    clus_cnt = 0;
    return *this;
}
DirEnt *DirEnt::entSearch(string a_dirname, uint *a_off) {
    // if(mount_flag == true) { return spblk->getFATRoot()->entSearch(a_dirname, a_off); }
    // 当前“目录”非目录
    if (!(attribute & ATTR_DIRECTORY)) { panic("dirLookUp not DIR"); }
    if (attribute & ATTR_LINK){
        struct Link li;
        spblk->rwClus(first_clus, false, false, (uint64)&li, 0, 36);
        first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        attribute = li.de.sne.attr;
    }
    // @todo 判断移到Path中
    // '.'表示当前目录，则增加当前目录引用计数并返回当前目录
    if (a_dirname == ".") { return entDup(); }
    // '..'表示父目录，则增加当前目录的父目录引用计数并返回父目录；如果当前是根目录则同'.'
    else if (a_dirname == "..") {
        if (this == spblk->getFATRoot()) { return this; }
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
        spblk->rwClus(first_clus, false, false, (uint64)&li, 0, 36);
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
        auto bytes = spblk->rwClus(cur_clus, false, false, (uint64)&de, off2, 32);
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
    int clus_num = a_off / spblk->rBPC();
    while (clus_num > clus_cnt) {
        int clus = spblk->fatRead(cur_clus);
        if (clus >= FAT32_EOC) {
            if (a_alloc) {
                clus = allocClus();
                spblk->fatWrite(cur_clus, clus);
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
            cur_clus = spblk->fatRead(cur_clus);
            if (cur_clus >= FAT32_EOC) { panic("relocClus"); }
            clus_cnt++;
        }
    }
    return a_off % spblk->rBPC();
}
const uint32 DirEnt::allocClus() const {  // @todo 应该写成FileSystem的成员？
    // should we keep a free cluster list? instead of searching fat every time.
    struct buf *b;
    uint32 sec = spblk->rRSC();
    uint32 const ent_per_sec = spblk->rBPS() / sizeof(uint32);
    for (uint32 i = 0; i < spblk->rFS(); i++, sec++) {
        auto ref = bcache[{spblk->rDev(), sec}];
        auto buf = *ref;
        for (uint32 j = 0; j < ent_per_sec; j++) {
            if (buf.as<uint32_t>(j) == 0) {
                buf.as<uint32_t>(j) = FAT32_EOC + 7;
                uint32 clus = i * ent_per_sec + j;
                spblk->clearClus(clus);
                return clus;
            }
        }
    }
    panic("no clusters");
}
DirEnt *DirEnt::entDup() {
    ++ref;
    return this;
}
DirEnt *DirEnt::eCacheHit(string a_name) const {  // @todo 重构ecache，写成ecache的成员
    DirEnt *head = &(ecache.entries[0]);
    for (DirEnt *ep = head->next; ep != head; ep = ep->next) {  // LRU algo
        if (ep->valid == 1 && ep->parent == this && strncmpamb(ep->filename, a_name.c_str(), FAT32_MAX_FILENAME) == 0) {  // @todo 不区分大小写？
            if (ep->ref++ == 0) { ep->parent->ref++; }
            return ep;
        }
    }
    return eCacheAlloc(spblk);
}
void DirEnt::entRelse() {
    // @todo 重构链表操作
    DirEnt *root = spblk->getFATRoot();
    if (this != root && valid != 0 && ref == 1) {
        // ref == 1 means no other process can have entry locked,
        // so this acquiresleep() won't block (or deadlock).
        next->prev = prev;
        prev->next = next;
        next = root->next;
        prev = root;
        root->next->prev = this;
        root->next = this;
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
            uint32 next = spblk->fatRead(clus);
            spblk->freeClus(clus);
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
    spblk->rwClus(parent->cur_clus, 0, 0, (uint64) &entcnt, poff, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    poff = parent->relocClus(off + (entcnt<<5), 0);
    union Ent de;
    spblk->rwClus(parent->cur_clus, 0, 0, (uint64)&de, poff, sizeof(de));
    de.sne.fst_clus_hi = (uint16)(first_clus >> 16);
    de.sne.fst_clus_lo = (uint16)(first_clus & 0xffff);
    de.sne.file_size = file_size;
    spblk->rwClus(parent->cur_clus, 1, 0, (uint64)&de, poff, sizeof(de));
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
        spblk->rwClus(cur_clus, true, false, (uint64)&de, a_off, sizeof(de));
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
            spblk->rwClus(cur_clus, true, false, (uint64)&de, off2, sizeof(de));
            a_off += sizeof(de);
        }
        memset(&de, 0, sizeof(de));
        strncpy(de.sne.name, shortname.c_str(), sizeof(de.sne.name));
        de.sne.attr = a_entry->attribute;
        de.sne.fst_clus_hi = (uint16)(a_entry->first_clus >> 16);  // first clus high 16 bits
        de.sne.fst_clus_lo = (uint16)(a_entry->first_clus & 0xffff);  // low 16 bits
        de.sne.file_size = a_entry->file_size;  // filesize is updated in dirUpdate()
        a_off = relocClus(a_off, true);
        spblk->rwClus(cur_clus, true, false, (uint64)&de, a_off, sizeof(de));
    }
}
int DirEnt::entRead(bool a_usrdst, uint64 a_dst, uint a_off, uint a_len) {
    if (a_off > file_size || a_off + a_len < a_off || (attribute & ATTR_DIRECTORY)) { return 0; }
    if (attribute & ATTR_LINK){
        struct Link li;
        spblk->rwClus(first_clus, false, false, (uint64)&li, 0, 36);
        first_clus = ((uint32)(li.de.sne.fst_clus_hi)<<16) + li.de.sne.fst_clus_lo;
        attribute = li.de.sne.attr;
    }
    if (a_off + a_len > file_size) { a_len = file_size - a_off; }
    uint tot, m;
    for (tot = 0; cur_clus < FAT32_EOC && tot < a_len; tot += m, a_off += m, a_dst += m) {
        relocClus(a_off, false);
        m = spblk->rBPC() - a_off % spblk->rBPC();
        if (a_len - tot < m) { m = a_len - tot; }
        if (spblk->rwClus(cur_clus, false, a_usrdst, a_dst, a_off % spblk->rBPC(), m) != m) { break; }
    }
    return tot;
}
int DirEnt::entWrite(bool a_usrsrc, uint64 a_src, uint a_off, uint a_len) {
    if (a_off > file_size || a_off + a_len < a_off || (uint64)a_off + a_len > 0xffffffff || (attribute & ATTR_READ_ONLY)) { return -1; }
    if (attribute & ATTR_LINK){
        struct Link li;
        spblk->rwClus(first_clus, false, false, (uint64)&li, 0, 36);
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
        m = spblk->rBPC() - a_off % spblk->rBPC();
        if (a_len - tot < m) { m = a_len - tot; }
        if (spblk->rwClus(cur_clus, true, a_usrsrc, a_src, a_off % spblk->rBPC(), m) != m) { break; }
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
    parent->spblk->rwClus(parent->cur_clus, false, false, (uint64)&entcnt, off2, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    uint8 flag = EMPTY_ENTRY;
    for (int i = 0; i <= entcnt; i++) {
        parent->spblk->rwClus(parent->cur_clus, true, false, (uint64)&flag, off2, 1);
        off1 += 32;
        off2 = parent->relocClus(off1, false);
    }
    valid = -1;
    return;
}
int DirEnt::entLink(DirEnt *a_entry) const {
    if(a_entry == nullptr ) { return -1; }
    DirEnt *parent1 = parent;
    int off2 = parent1->relocClus(off, false);
    union Ent de;
    if (parent1->spblk->rwClus(parent1->cur_clus, false, false, (uint64)&de, off2, 32) != 32 || de.lne.order == END_OF_ENTRY) {
        printf("can't read Ent\n");
        return -1;
    }
    struct Link li;
    int clus;
    if(!(de.sne.attr & ATTR_LINK)){
        clus = allocClus();
        li.de = de;
        li.link_count = 2;
        if(spblk->rwClus(clus, true, false, (uint64)&li, 0, 36) != 36){
            printf("write li wrong\n");
            return -1;
        }
        de.sne.attr = ATTR_DIRECTORY | ATTR_LINK;
        de.sne.fst_clus_hi = (uint16)(clus >> 16);       
        de.sne.fst_clus_lo = (uint16)(clus & 0xffff);
        de.sne.file_size = 36;
        if(parent1->spblk->rwClus(parent1->cur_clus, true, false, (uint64)&de, off2, 32) != 32){
            printf("write parent1 wrong\n");
            return -1;
        }
    }
    else {
        clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
        spblk->rwClus(clus, false, false, (uint64)&li, 0, 36);
        li.link_count++;
        if(spblk->rwClus(clus, true, false, (uint64)&li, 0, 36) != 36){
            printf("write li wrong\n");
            return -1;
        }
    }
    uint off = 0;
    DirEnt *ep = a_entry->entSearch(a_entry->filename, &off);
    if(ep != nullptr) {
        printf("%s exits", a_entry->filename);
        return -1;
    }
    off = a_entry->relocClus(off, true);
    if(a_entry->spblk->rwClus(a_entry->cur_clus, true, false, (uint64)&de, off, 32) != 32){
        printf("write de into %s wrong",a_entry->filename);
        return -1;
    }
    return 0;
}
int DirEnt::entUnlink() const {
    DirEnt *eparent = parent;
    int off2 = eparent->relocClus(off, false);
    union Ent de;
    if (eparent->spblk->rwClus(eparent->cur_clus, false, false, (uint64)&de, off2, 32) != 32 || de.lne.order == END_OF_ENTRY) {
        printf("can't read Ent\n");
        return -1;
    }
    if(de.sne.attr & ATTR_LINK) {
        int clus;
        struct Link li;
        clus = ((uint32)(de.sne.fst_clus_hi) << 16) + (uint32)(de.sne.fst_clus_lo);
        if(spblk->rwClus(clus, false, false, (uint64)&li, 0, 36) != 36) {
            printf("read li wrong\n");
            return -1;
        }
        if(--li.link_count == 0){
            spblk->freeClus(clus);
            de = li.de;
            if(eparent->spblk->rwClus(eparent->cur_clus, true, false, (uint64)&de, off2, 32) != 32){
                printf("write de into %s wrong\n",eparent->filename);
                return -1;
            }
        }
    }
    return 0;
}
// int DEntry::entMount(shared_ptr<DEntry> a_dev) const {
//     // while(dev_fat[mount_num].isValid()) {
//     //     ++mount_num;
//     //     mount_num = mount_num % 8;
//     // }
//     uint8 mount_num = dev_table.size();
//     DirEnt *root = eCacheAlloc(mount_num);
//     *root = DirEnt("/", ATTR_DIRECTORY|ATTR_SYSTEM, 0, nullptr, root->next, root->prev);
//     shared_ptr<FileSystem> nfs;
//     {  // eliminate lifecycle
//         auto buf = bcache[{ a_dev->getINode()->rDev(), 0 }];
//         nfs = make_shared<FileSystem>(*buf, true, make_shared<DEntry>(root), true);
//         root->spblk = nfs->getSpBlk();
//         dev_table[mount_num] = nfs;
//     }
//     // make sure that byts_per_sec has the same value with BlockBuf::blockSize 
//     if (BlockBuf::blockSize != nfs.rBPS()) { panic("byts_per_sec != BlockBuf::blockSize"); }
//     root->first_clus = root->cur_clus = nfs.rRC();
//     // DirEnt *root = dev_fat[mount_num].getRoot();
//     // *root = { {'/','\0'}, ATTR_DIRECTORY|ATTR_SYSTEM, dev_fat[mount_num].rRC(), 0, dev_fat[mount_num].rRC(), 0, mount_num, false, 1, 0, 0, nullptr, root, root, false };
//     entry->mount_flag = true;
//     entry->spblk = root->spblk;
//     return 0;
// }
// int DEntry::entUnmount() const {
//     entry->mount_flag = false;
//     // memset(&dev_fat[entry->dev], 0, sizeof(dev_fat[entry->dev]));
//     dev_table.erase(entry->spblk->rDev());
//     entry->spblk = entry->spblk->getMntParent();
//     return 0;
// }
const uint SuperBlock::rwClus(uint32 a_cluster, bool a_iswrite, bool a_usrbuf, uint64 a_buf, uint a_off, uint a_len) const {
    if (a_off + a_len > rBPC()) { panic("offset out of range"); }
    uint tot, m;
    uint sec = firstSec(a_cluster) + a_off / rBPS();
    a_off = a_off % rBPS();
    int bad = 0;
    for (tot = 0; tot < a_len; tot += m, a_off += m, a_buf += m, sec++) {
        auto bp = bcache[{dev, sec}];  // @todo 设备号？
        m = BlockBuf::blockSize - a_off % BlockBuf::blockSize;
        if (a_len - tot < m) { m = a_len - tot; }
        // @todo 弃用bufCopyIn/Out()
        if (a_iswrite) {
            if ((bad = bufCopyIn(bp->d + (a_off % BlockBuf::blockSize), a_usrbuf, a_buf, m)) != -1) {bp->dirty=1;}
        }
        else { bad = bufCopyOut(a_usrbuf, a_buf, bp->d + (a_off % BlockBuf::blockSize), m); }
        if (bad == -1) { break; }
    }
    return tot;
}
const uint32 SuperBlock::fatRead(uint32 a_cluster) const {
    if (a_cluster >= FAT32_EOC) { return a_cluster; }
    if (a_cluster > rDCC() + 1) { return 0; }  // because cluster number starts at 2, not 0
    uint32 fat_sec = numthSec(a_cluster, 1);
    // here should be a cache layer for FAT table, but not implemented yet.
    auto buf = bcache[{0,fat_sec}];
    uint32 next_clus = (*buf)[secOffset(a_cluster)];
    return next_clus;
}
void SuperBlock::clearClus(uint32 a_cluster) const {
    uint32 sec = firstSec(a_cluster);
    for (int i = 0; i < rSPC(); i++) {
        auto buf = bcache[{0, sec++}];
        buf->clear();
    }
    return;
}
const int SuperBlock::fatWrite(uint32 a_cluster, uint32 a_content) const {
    if (a_cluster > rDCC()+1) { return -1; }
    uint32 fat_sec = numthSec(a_cluster, 1);
    auto buf = bcache[{0, fat_sec}];
    uint off = secOffset(a_cluster);
    (*buf)[off] = a_content;
    return 0;
}
// inline DirEnt *FileSystem::getFATRoot() const { return root->rawPtr(); }
int FileSystem::ldSpBlk(shared_ptr<fs::DEntry> a_dev) {
    DirEnt *root = eCacheAlloc();
    *root = DirEnt("/", ATTR_DIRECTORY|ATTR_SYSTEM, 0, nullptr, root->next, root->prev);
    {  // eliminate lifecycle
        auto buf = bcache[{ a_dev->getINode()->rDev(), 0 }];
        root->spblk = spblk = make_shared<SuperBlock>(make_shared<DEntry>(root), this, a_dev->getINode()->rDev(), buf);
    }
    if (BlockBuf::blockSize != spblk.rBPS()) { panic("byts_per_sec != BlockBuf::blockSize"); }
    root->first_clus = root->cur_clus = spblk.rRC();
    return 0;
}