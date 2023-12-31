// #include "fs.hh"
#include "fat.hh"
#include "vm/pager.hh"
#include "vm/vmo.hh"
#include "proc.hh"
#include "kernel.hh"
// #include "klib.h"

#define moduleLevel LogLevel::trace

// #define FMT_PROC(fmt,...) Log(info,"Proc[%d]::\n\"\n" fmt "\n\"",kHartObj().curtask->getProcess()->pid(),__VA_ARGS__)
#define FMT_PROC(fmt,...) printf(fmt,__VA_ARGS__)

// @todo error handling
using namespace fs;

using vm::pageSize;

unordered_map<string, shared_ptr<FileSystem>> mnt_table;
static constexpr int SEEK_SET = 0;
static constexpr int SEEK_CUR = 1;
static constexpr int SEEK_END = 2;

// DStat::DStat(shared_ptr<File> a_file) {
//     switch(a_file->obj.rType()) {
//         case dev:
//             *this = DStat(a_file->obj.rType());
//             break;
//         case pipe:
//             *this = DStat(a_file->obj.getPipe());
//             break;
//         case entry:
//             *this = DStat(a_file->obj.getEntry());
//             break;
//         default:
//             *this = DStat(a_file->obj.rType());
//             break;
//     }
//     return;
// }

int File::write(ByteArray a_buf){
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.w) { return rt; }
    Log(debug,"write(%d bytes)",a_buf.len);
    switch(obj.rType()){
        case FileType::stdout:
        case FileType::stderr:
            FMT_PROC("%s", string(a_buf.c_str(), a_buf.len).c_str());
            rt = a_buf.len;
            break;
        case FileType::pipe:
            obj.getPipe()->write(a_buf);
            rt = a_buf.len;
            break;
        case FileType::entry:
            if (obj.getEntry()->getINode()->nodWrite((xlen_t)a_buf.buff, obj.off(), a_buf.len) == a_buf.len) {
                obj.off() += a_buf.len;
                rt = a_buf.len;
            }
            else { rt = sys::statcode::err; }
        default:
            break;
    }
    return rt;
}
int File::read(ByteArray buf, long a_off, bool a_update){  // @todo: 重构一下
    xlen_t rt=sys::statcode::err;
    if(!ops.fields.r) { return rt; }
    switch(obj.rType()){
        case FileType::stdin:
            panic("unimplementd read type stdin\n");
            break;
        case FileType::pipe:
            return obj.getPipe()->read(buf);
            break;
        case FileType::dev:
            panic("unimplementd read type dev\n");
            break;
        case FileType::entry: {
            if(a_off < 0) { a_off = obj.off(); }
            if(auto rdbytes = obj.getEntry()->getINode()->nodRead((uint64)buf.buff, a_off, buf.len); rdbytes > 0) {
                if(a_update) { obj.off() = a_off + rdbytes; }
                return rdbytes;
            }
            break;
        }
        default:
            panic("File::read(): unknown file type");
            break;
    }
    return 0;
}
ByteArray File::readAll(){
    switch(obj.rType()){
        case FileType::entry:{
            size_t size=obj.getEntry()->getINode()->rFileSize();
            return read(size);
        }
        default:
            panic("readAll doesn't support this type");
    }
}
off_t File::lSeek(off_t a_offset, int a_whence) {
    if (S_ISCHR(obj.kst().st_mode) || S_ISIFO(obj.kst().st_mode)) { return 0; }
    // if(type!=FileType::entry)return -ESPIPE;
    KStat kst = obj.getEntry();
    switch (a_whence) {  // @todo: st_size处是否越界？
        case SEEK_SET: {
            if(a_offset<0 || a_offset>kst.st_size) { return -EINVAL; }
            obj.off() = a_offset;
            break;
        }
        case SEEK_CUR: {
            off_t noff = obj.off() + a_offset;
            if(noff<0 || noff>kst.st_size) { return -EINVAL; }
            obj.off() = noff;
            break;
        }
        case SEEK_END: {
            off_t noff = kst.st_size + a_offset;
            if(noff<0 || noff>kst.st_size) { return -EINVAL; }
            obj.off() = noff;
            break;
        }
        default: { return -EINVAL; }
    }
    return obj.off();
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
        auto buf_=(uint8_t*)vm::pn2addr(kGlobObjs->pageMgr->alloc(1));
        ByteArray buf(buf_,rem);
        rem=read(buf);  // @todo: 安全检查
        if(rem==0) break;
        int ret = a_outfile->write(ByteArray(buf_,rem));
        kGlobObjs->pageMgr->free(vm::addr2pn((xlen_t)buf_),0);
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
    switch(obj.rType()){
        case FileType::pipe: {
            if(ops.fields.r)obj.getPipe()->decReader();
            else if(ops.fields.w)obj.getPipe()->decWriter();
            obj.getPipe().reset();
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
    if(pathname.size() < 1) { base = kHartObj().curtask->getProcess()->cwd; return; }
    else if(pathname[0] == '/') { base = mnt_table["/"]->getSpBlk()->getRoot(); }
    else if(base == nullptr) { base = kHartObj().curtask->getProcess()->cwd; }
    size_t len = pathname.size();
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
    for(shared_ptr<DEntry> entry = base; !(entry->isRoot() && entry->getINode()->getSpBlk()->getFS()->isRootFS()); entry = entry->getParent()) { name_abs.emplace(name_abs.begin(), entry->rName()); }
    string path_abs = "";
    for(auto name = name_abs.begin(); name != name_abs.end();) {
        if(*name=="." || *name=="..") { name = name_abs.erase(name); }
        else if(next(name)!=name_abs.end() && *(next(name))=="..") { name = name_abs.erase(name); }
        else {
            path_abs = path_abs + "/" + *name;
            ++name;
        }
    }
    if(path_abs == "") { path_abs = "/"; }
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
        /// @todo refactor
        if(!pathname.length())dirname.clear();
        else pathBuild();
        return entry;
    }
    else { return nullptr; }
}
shared_ptr<DEntry> Path::pathSearch(bool a_parent) {
    if(pathname == "/") { return mnt_table["/"]->getSpBlk()->getRoot(); }  // 防止初始化进程时循环依赖cwd
    shared_ptr<DEntry> entry, next;
    if(pathname.length() < 1) { return nullptr; }  // 空路径
    else if((entry=pathHitTable()) != nullptr) {}  // 查挂载表，若查到则起始目录存到entry中
    // else if(pathname[0] == '/') { entry = mnt_table["/"]->getSpBlk()->getRoot(); }  // 绝对路径
    else { entry = base; }  // 相对路径
    int dirnum = dirname.size();
    for(int i = 0; i < dirnum; ++i) {
        Log(trace,"entry=",entry->rName().c_str());
        while(entry->isMntPoint()) panic("should be processed above");
        if (!S_ISDIR(entry->getINode()->rMode())) { return nullptr; }
        if (a_parent && i == dirnum-1) { return entry; }
        if(dirname[i] == ".") { next = entry; }
        else if(dirname[i] == "..") { next = entry->getParent(); }
        else {
            if(auto sub=entry->entSearch(entry,dirname[i]))
                next=sub.value();
            else return nullptr;
        }
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
    if (a_type == T_DIR) { a_mode |= S_IFDIR; }
    else a_mode|=S_IFREG;
    if(auto rt = dp->entCreate(dp,dirname.back(), a_mode)){
        auto ep=rt.value();
        if ((a_type==T_DIR && !S_ISDIR(ep->getINode()->rMode())) || (a_type==T_FILE && S_ISDIR(ep->getINode()->rMode()))) { return nullptr; }
        return ep;
    } else return nullptr;
}
int Path::pathRemove() {
    shared_ptr<DEntry> ep = pathSearch();
    if(ep == nullptr) { return -1; }
    if(S_ISDIR(ep->getINode()->rMode()) && !ep->getINode()->isEmpty()) { return -1; }
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
    if(!S_ISDIR(ep->getINode()->rMode())) {
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
int Path::mount(shared_ptr<FileSystem> fs){
    if(auto dentry=pathSearch()){
        auto pathabs=pathAbsolute();
        mnt_table[pathabs]=fs;
        return 0;
    }
    return -1;
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
int Path::pathOpen(int a_flags, mode_t a_mode) {  // @todo: 添加不打开额外文件的工作方式
    auto entry=pathSearch();
    if(!entry && a_flags & O_CREAT)
        entry=pathCreate(S_ISDIR(a_mode)?T_DIR:T_FILE, a_flags);
    if(!entry)
        return -1;
    if(S_ISDIR(entry->getINode()->rMode()) && ((a_flags&O_RDWR) || (a_flags&O_WRONLY))) {
        Log(error, "try to open a dir as writable\n");
        return -EISDIR;
    }
    if((a_flags&O_DIRECTORY) && !S_ISDIR(entry->getINode()->rMode())) {
        Log(error, "try to open a not dir file as dir\n");
        return -ENOTDIR;
    }
    shared_ptr<File> file = make_shared<File>(entry, a_flags);
    file->obj.off() = (a_flags&O_APPEND) ? entry->getINode()->rFileSize() : 0;
    int fd = kHartObj().curtask->getProcess()->fdAlloc(file);
    if(fd < 0) { return -1; }
    if(!S_ISDIR(entry->getINode()->rMode()) && (a_flags&O_TRUNC)) { entry->getINode()->nodTrunc(); }

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
    else if(!S_ISDIR(entry->getINode()->rMode())) {
        Log(error, "parent not dir\n");
        return -1;
    }
    return entry->getINode()->entSymLink(a_target);
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

#include "scatterio.hh"
namespace fs
{
    void INode::readv(const memvec &src,const memvec &dst){
        SingleFileScatteredReader fio(*this,src);
        KMemScatteredIO mio(dst);
        scatteredCopy(mio,fio);
    }
    void INode::readPages(const memvec &src,const memvec &dst){
        memvec src1,dst1;
        for(auto seg:src)
            src1.push_back(Segment<xlen_t>{vm::pn2addr(seg.l),vm::pn2addr(seg.r+1)-1});
        for(auto seg:dst)
            dst1.push_back(Segment<xlen_t>{vm::pn2addr(seg.l),vm::pn2addr(seg.r+1)-1});
        return readv(src1,dst1);
    }

    Result<DERef> DEntry::entSearch(DERef self,string a_dirname, uint *a_off){
        Log(debug,"lookup(%s)",a_dirname.c_str());
        if(auto it=subs.find(a_dirname); it!=subs.end() && !it->second.expired())
            return it->second.lock();
        if(auto subnod=nod->lookup(a_dirname,a_off)){
            Log(trace,"dentry caching %s",a_dirname.c_str());
            auto sub=make_shared<DEntry>(self,a_dirname,subnod);
            subs[a_dirname]=weak_ptr<DEntry>(sub);
            return sub;
        }
        return make_unexpected(-1);
    }
    Result<DERef> DEntry::entCreate(DERef self,string a_name, mode_t mode){
        if(auto it=subs.find(a_name); it!=subs.end())
            return make_unexpected(-1);
        if(auto subnod=nod->mknod(a_name,mode)){
            auto sub=make_shared<DEntry>(self,a_name,subnod);
            subs[a_name]=weak_ptr<DEntry>(sub);
            return sub;
        }
        return make_unexpected(-1);
    }

list<shared_ptr<vm::VMO>> vmolru;

    shared_ptr<vm::VMO> File::vmo(){
        auto vnode=obj.getEntry()->getINode();
        if(vnode->vmo.expired()){
            auto rt=make_shared<vm::VMOPaged>(vm::bytes2pages(vnode->rFileSize()),eastl::dynamic_pointer_cast<vm::Pager>(make_shared<vm::VnodePager>(vnode)));
            vnode->vmo=eastl::weak_ptr<vm::VMOPaged>(rt);
            if(vmolru.size()>10)vmolru.pop_front();
            vmolru.push_back(rt);
            return rt;
        }
        return vnode->vmo.lock();
    }
    size_t File::readv(ScatteredIO &dst){
        switch(obj.rType()){
        case FileType::entry:{
            vector<Slice> fvec={{obj.off(),obj.getEntry()->getINode()->rFileSize()}};
            SingleFileScatteredReader fio(*obj.getEntry()->getINode(),fvec);
            auto rdbytes=scatteredCopy(dst,fio);
            obj.off()+=rdbytes;
            return rdbytes;
        } break;
        case FileType::stdin:
        default:
            ;
        }
        return -1;
    }
    size_t File::writev(ScatteredIO &dst){
        switch(obj.rType()){
        case FileType::entry:{
            vector<Slice> fvec={{obj.off(),obj.getEntry()->getINode()->rFileSize()}};
            SingleFileScatteredWriter fio(*obj.getEntry()->getINode(),fvec);
            auto wbytes=scatteredCopy(dst,fio);
            obj.off()+=wbytes;
            return wbytes;
            } break;
        case FileType::pipe:{
            PipeScatteredWriter pio(*obj.getPipe());
            return scatteredCopy(dst,pio);
        } break;
        case FileType::stdout:
        case FileType::stderr:{
            PrintScatteredWriter pio;
            return scatteredCopy(dst,pio);
        } break;
        default:
            ;
        }
        return -1;
    }
    bool File::isRReady(){
        switch(obj.rType()){
            case FileType::pipe:
                return obj.getPipe()->readable();
            case FileType::entry:
                return true;
            default:
                return false;
        }
    }
    bool File::isWReady(){
        switch(obj.rType()){
            case FileType::pipe:
                return obj.getPipe()->writable();
            case FileType::entry:
                return true;
            default:
                return false;
        }
    }
} // namespace fs
