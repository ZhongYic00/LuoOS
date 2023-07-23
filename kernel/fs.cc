// #include "fs.hh"
#include "fat.hh"
// #include "vm.hh"
#include "proc.hh"
#include "kernel.hh"
// #include "klib.h"

// #define moduleLevel LogLevel::debug

// #define FMT_PROC(fmt,...) Log(info,"Proc[%d]::\n\"\n" fmt "\n\"",kHartObj().curtask->getProcess()->pid(),__VA_ARGS__)
#define FMT_PROC(fmt,...) printf(fmt,__VA_ARGS__)

// @todo error handling
using namespace fs;

using vm::pageSize;

static unordered_map<string, shared_ptr<FileSystem>> mnt_table;
static constexpr int SEEK_SET = 0;
static constexpr int SEEK_CUR = 1;
static constexpr int SEEK_END = 2;

xlen_t File::write(xlen_t addr,size_t len){
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.w)return rt;
    auto bytes=kHartObj().curtask->getProcess()->vmar.copyin(addr,len);
    Log(debug,"write(%d bytes)",bytes.len);
    switch(type){
        case FileType::stdout:
        case FileType::stderr:
            FMT_PROC("%s",klib::string(bytes.c_str(),len).c_str());
            rt=bytes.len;
            break;
        case FileType::pipe:
            obj.pipe->write(bytes);
            rt=bytes.len;
            break;
        case FileType::entry:
            if (obj.ep->getINode()->nodWrite(true, addr, off, len) == len) {
                off += len;
                rt = len;
            }
            else { rt = sys::statcode::err; }
        default:
            break;
    }
    return rt;
}
ByteArray File::read(size_t len, long a_off, bool a_update){
    if(a_off < 0) { a_off = off; }
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.r) { return rt; }
    switch(type){
        case FileType::stdin:
            panic("unimplementd!");
            break;
        case FileType::pipe:
            return obj.pipe->read(len);
            break;
        case FileType::dev:
            panic("unimplementd!");
            break;
        case FileType::entry: {
            int rdbytes = 0;
            ByteArray buf(len);
            if((rdbytes = obj.ep->getINode()->nodRead(false, (uint64)buf.buff, a_off, len)) > 0) {
                if(a_update) { off = a_off + rdbytes; }
            }
            return ByteArray(buf.buff, rdbytes);
            break;
        }
        default:
            panic("File::read(): unknown file type");
            break;
    }
    return ByteArray{0};
}
ByteArray File::readAll(){
    switch(type){
        case FileType::entry:{
            size_t size=obj.ep->getINode()->rFileSize();
            return read(size);
        }
        default:
            panic("readAll doesn't support this type");
    }
}
off_t File::lSeek(off_t a_offset, int a_whence) {
    KStat kst = obj.ep;
    if ((kst.st_mode&S_IFMT)==S_IFCHR || (kst.st_mode&S_IFMT)==S_IFIFO) { return 0; }
    switch (a_whence) {  // @todo: st_size处是否越界？
        case SEEK_SET: {
            if(a_offset<0 || a_offset>kst.st_size) { return -EINVAL; }
            off = a_offset;
            break;
        }
        case SEEK_CUR: {
            off_t noff = off + a_offset;
            if(noff<0 || noff>kst.st_size) { return -EINVAL; }
            off = noff;
            break;
        }
        case SEEK_END: {
            off_t noff = kst.st_size + a_offset;
            if(noff<0 || noff>kst.st_size) { return -EINVAL; }
            off = noff;
            break;
        }
        default: { return -EINVAL; }
    }
    return off;
}
ssize_t File::sendFile(shared_ptr<File> a_outfile, off_t *a_offset, size_t a_len) {
    uint64_t in_off = 0;
    if(a_offset != nullptr) {
        in_off = a_outfile->lSeek(0, SEEK_CUR);
        if((ssize_t)in_off < 0) { return (ssize_t) in_off; } // this fails only if in_fd is invalid
        lSeek(*a_offset, SEEK_SET); // and this must success
    }
    ssize_t nsend = 0;
    while (a_len > 0) {
        ssize_t rem = (ssize_t)(a_len > pageSize ? pageSize : a_len);
        ByteArray buf = read(rem);
        int ret = buf.len;
        ret = a_outfile->write((xlen_t)buf.buff, ret);
        if (ret < 0) { return ret; }
        nsend += ret;
        if (rem != ret) { break; } // EOF reached in in_fd or out_fd is full
        a_len -= rem;
    }
    if (a_offset != nullptr) {
        *a_offset = lSeek(0, SEEK_CUR);
        lSeek(in_off, SEEK_SET);
    }
    return nsend;
}
File::~File() {
    switch(type){
        case FileType::pipe: {
            if(ops.fields.r)obj.pipe->decReader();
            else if(ops.fields.w)obj.pipe->decWriter();
            obj.pipe.reset();
            break;
        }
        case FileType::entry: {
            break;
        }
        case FileType::dev: {
            break;
        }
    }
}
void Path::pathBuild() {
    if(pathname.length() < 1) { base = kHartObj().curtask->getProcess()->cwd; return; }
    else if(pathname[0] == '/') { base = mnt_table["/"]->getSpBlk()->getRoot(); }
    else if(base == nullptr) { base = kHartObj().curtask->getProcess()->cwd; }
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
string Path::pathAbsolute() const {
    vector<string> name_abs = dirname;
    for(shared_ptr<DEntry> entry = base; (!entry->isRoot())||(!entry->getINode()->getSpBlk()->getFS()->isRootFS()); entry = entry->getParent()) { name_abs.emplace(name_abs.begin(), entry->rName()); }
    string path_abs = "";
    for(string name : name_abs) { path_abs = path_abs + "/" + name; }
    return path_abs;
}
shared_ptr<DEntry> Path::pathHitTable() {
    string path_abs = pathAbsolute();
    size_t len = path_abs.size();
    size_t longest = 0;
    string longest_prefix = "";
    for(auto tbl : mnt_table) {
        string prefix = tbl.first;
        size_t len_prefix = prefix.size();
        if(len>=len_prefix && len_prefix>longest && path_abs.compare(0, len_prefix, prefix)==0) {
            longest = len_prefix;
            longest_prefix = prefix;
        }
    }
    if(longest > 0) {  // 最长前缀
        shared_ptr<DEntry> entry = mnt_table[longest_prefix]->getSpBlk()->getRoot();
        base = entry;
        pathname = path_abs.substr(longest);
        pathBuild();
        return entry;
    }
    else { return nullptr; }
}
shared_ptr<DEntry> Path::pathSearch(bool a_parent) {
    if(pathname == "/") { return mnt_table["/"]->getSpBlk()->getRoot(); }  // 防止初始化进程时循环依赖cwd
    shared_ptr<DEntry> entry, next;
    int dirnum = dirname.size();
    if(pathname.length() < 1) { return nullptr; }  // 空路径
    else if((entry=pathHitTable()) != nullptr) {}  // 查挂载表，若查到则起始目录存到entry中
    // else if(pathname[0] == '/') { entry = mnt_table["/"]->getSpBlk()->getRoot(); }  // 绝对路径
    else { entry = base; }  // 相对路径
    for(int i = 0; i < dirnum; ++i) {
        while(entry->isMntPoint()) { entry = entry->getINode()->getSpBlk()->getRoot(); }
        if (!(entry->getINode()->rAttr() & ATTR_DIRECTORY)) { return nullptr; }
        if (a_parent && i == dirnum-1) { return entry; }
        if(dirname[i] == ".") { next = entry; }
        else if(dirname[i] == "..") { next = entry->getParent(); }
        else { next = entry->entSearch(dirname[i]); }
        if (next == nullptr) { return nullptr; }
        entry = next;
    }
    return entry;
}
shared_ptr<DEntry> Path::pathCreate(short a_type, int a_mode) {  // @todo 改成返回File
    shared_ptr<DEntry> dp = pathSearch(true);
    if(dp == nullptr){
        printf("can't find dir\n");
        return nullptr;
    }
    if (a_type == T_DIR) { a_mode = ATTR_DIRECTORY; }
    else if (a_mode & O_RDONLY) { a_mode = ATTR_READ_ONLY; }
    else { a_mode = 0; }
    shared_ptr<DEntry> ep = dp->entCreate(dirname.back(), a_mode);
    if (ep == nullptr) { return nullptr; }
    if ((a_type==T_DIR && !(ep->getINode()->rAttr()&ATTR_DIRECTORY)) || (a_type==T_FILE && (ep->getINode()->rAttr()&ATTR_DIRECTORY))) { return nullptr; }
    return ep;
}
int Path::pathRemove() {
    shared_ptr<DEntry> ep = pathSearch();
    if(ep == nullptr) { return -1; }
    if((ep->getINode()->rAttr() & ATTR_DIRECTORY) && !ep->isEmpty()) { return -1; }
    ep->getINode()->nodRemove();
    return 0;
}
int Path::pathHardLink(Path a_newpath) {
    shared_ptr<INode> ino1 = pathSearch()->getINode();
    shared_ptr<INode> ino2 = a_newpath.pathSearch()->getINode();
    if(ino1==nullptr || ino2==nullptr) {
        printf("can't find dir\n");
        return -1;
    }
    string fs1 = ino1->getSpBlk()->getFS()->rFSType();
    string fs2 = ino2->getSpBlk()->getFS()->rFSType();
    if(fs1 != fs2) {
        printf("different filesystem\n");
        return -1;
    }
    if(fs1=="fat32" || fs1=="vfat") {
        fat::INode *fatino1 = (fat::INode*)(ino1.get());
        fat::INode *fatino2 = (fat::INode*)(ino2.get());
        if(fatino1->nodHardLink(fatino2) == -1) { return -1; }
        ino1.reset();
        ino2.reset();
    }
    // 其它文件系统在此补充
    else {
        printf("unsupported filesystem type\n");
        return -1;
    }
    return 0;
}
int Path::pathHardUnlink() {
    shared_ptr<DEntry> dp = pathSearch();
    if(dp == nullptr) { return -1; }
    if(dp->getINode()->nodHardUnlink() == -1) { return -1; }
    return pathRemove();
}
int Path::pathMount(Path a_devpath, string a_fstype) {
    shared_ptr<DEntry> ep = pathSearch();
    shared_ptr<DEntry> dev_ep = a_devpath.pathSearch();
    if(ep==nullptr || dev_ep==nullptr) {
        printf("DEntry not found\n");
        return -1;
    }
    if(ep->isRoot() || ep->isMntPoint()) {  //mountpoint not allowed the root
        printf("not allowed\n");
        return -1;
    }
    if(!(ep->getINode()->rAttr() & ATTR_DIRECTORY)) {
        printf("mountpoint is not a dir\n");
        return -1;
    }
    if(a_fstype=="fat32" || a_fstype=="vfat") {
        string path_abs = pathAbsolute();
        shared_ptr<fat::FileSystem> fatfs = make_shared<fat::FileSystem>(false, path_abs);
        mnt_table.emplace(path_abs, fatfs);
        if(fatfs->ldSpBlk(dev_ep->getINode()->rDev(), ep) == -1) {
            mnt_table[path_abs].reset();
            return -1;
        }
    }
    // 其它文件系统在此补充
    else {
        printf("unsupported filesystem type\n");
        return -1;
    }
    return 0;
}
int Path::pathUnmount() const {
    string path_abs = pathAbsolute();
    shared_ptr<FileSystem> umnt = mnt_table[path_abs];
    if(umnt->isRootFS()) {
        printf("not allowed\n");
        return -1;
    }
    shared_ptr<DEntry> mnt_point = umnt->getSpBlk()->getMntPoint();
    umnt->unInstall();
    mnt_table[path_abs].reset();
    mnt_table.erase(path_abs);
    // mnt_point->clearMnt();
    return 0;
}
int Path::pathOpen(int a_flags, mode_t a_mode) {
    shared_ptr<DEntry> entry;
    if(a_flags & O_CREATE) {
        entry = pathCreate(S_ISDIR(a_mode)?T_DIR:T_FILE, a_flags);
        if(entry == nullptr) { return -1; }
    }
    else {
        if((entry = pathSearch()) == nullptr) { return -1; }
        if((entry->getINode()->rAttr()&ATTR_DIRECTORY) && ((a_flags&O_RDWR) || (a_flags&O_WRONLY))) {
            printf("dir can't write\n");
            return -1;
        }
        if((a_flags&O_DIRECTORY) && !(entry->getINode()->rAttr()&ATTR_DIRECTORY)) {
            printf("it is not dir\n");
            return -1;
        }
    }
    shared_ptr<File> file = make_shared<File>(entry, a_flags);
    file->off = (a_flags&O_APPEND) ? entry->getINode()->rFileSize() : 0;
    int fd = kHartObj().curtask->getProcess()->fdAlloc(file);
    if(fd < 0) { return -1; }
    if(!(entry->getINode()->rAttr()&ATTR_DIRECTORY) && (a_flags&O_TRUNC)) { entry->getINode()->nodTrunc(); }

    return fd;
}
int Path::pathSymLink(string a_target) {
    shared_ptr<DEntry> entry = pathSearch();
    if(entry != nullptr) {
        Log(error, "linkpath exists\n");
        return -1;
    }
    entry = pathSearch(true);
    if(entry == nullptr) {
        Log(error, "dir not exists\n");
        return -1;
    }
    else if(!(entry->getINode()->rAttr() & ATTR_DIRECTORY)) {
        Log(error, "parent not dir\n");
        return -1;
    }
    return entry->entSymLink(a_target);
}
int fs::rootFSInit() {
    new ((void*)&mnt_table) unordered_map<string, shared_ptr<FileSystem>>();
    shared_ptr<FileSystem> rootfs = make_shared<fat::FileSystem>(true, "/");
    mnt_table.emplace("/", rootfs);
    if(rootfs->ldSpBlk(0, nullptr) == -1) {
        mnt_table["/"].reset();
        return -1;
    }
    return 0;
}
