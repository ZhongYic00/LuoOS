#include "fs.hh"
#include "vm.hh"
#include "kernel.hh"
// #include "klib.h"

// #define moduleLevel LogLevel::debug

// #define FMT_PROC(fmt,...) Log(info,"Proc[%d]::\n\"\n" fmt "\n\"",kHartObj().curtask->getProcess()->pid(),__VA_ARGS__)
#define FMT_PROC(fmt,...) printf(fmt,__VA_ARGS__)

// @todo error handling
namespace fs {
    map<uint8, shared_ptr<FileSystem>> dev_table;
}
using namespace fs;

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
klib::ByteArray File::read(size_t len, long a_off, bool a_update){
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
            klib::ByteArray buf(len);
            if((rdbytes = obj.ep->getINode()->nodRead(false, (uint64)buf.buff, a_off, len)) > 0) {
                if(a_update) { off = a_off + rdbytes; }
            }
            return klib::ByteArray(buf.buff, rdbytes);
            break;
        }
        default:
            panic("File::read(): unknown file type");
            break;
    }
    return klib::ByteArray{0};
}
klib::ByteArray File::readAll(){
    switch(type){
        case FileType::entry:{
            size_t size=obj.ep->getINode()->rFileSize();
            return read(size);
        }
        default:
            panic("readAll doesn't support this type");
    }
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
shared_ptr<DEntry> Path::pathSearch(shared_ptr<File> a_file, bool a_parent) const {  // @todo 改成返回File
    shared_ptr<DEntry> entry;
    int dirnum = dirname.size();
    if(pathname.length() < 1) { return nullptr; }  // 空路径
    else if(pathname[0] == '/') { entry = dev_table[0]->getSpBlk()->getRoot(); }  // 绝对路径
    else if(a_file != nullptr) { entry = a_file->obj.ep; }  // 相对路径（指定目录）
    else { entry = kHartObj().curtask->getProcess()->cwd; }  // 相对路径（工作目录）
    for(int i = 0; i < dirnum; ++i) {
        if (!(entry->getINode()->rAttr() & ATTR_DIRECTORY)) { return nullptr; }
        if (a_parent && i == dirnum-1) { return entry; }
        shared_ptr<DEntry> next = entry->entSearch(dirname[i]);
        if (next == nullptr) { return nullptr; }
        entry = next;
    }
    return entry;
}
shared_ptr<DEntry> Path::pathCreate(short a_type, int a_mode, shared_ptr<File> a_file) const {  // @todo 改成返回File
    shared_ptr<DEntry> dp = pathSearch(a_file, true);
    if(dp == nullptr){
        printf("can't find dir\n");
        return nullptr;
    }
    if (a_type == T_DIR) { a_mode = ATTR_DIRECTORY; }
    else if (a_mode & O_RDONLY) { a_mode = ATTR_READ_ONLY; }
    else { a_mode = 0; }
    shared_ptr<DEntry> ep = make_shared<DEntry>(dp->getINode()->nodCreate(dirname.back(), a_mode));
    if (ep == nullptr) { return nullptr; }
    if ((a_type==T_DIR && !(ep->getINode()->rAttr()&ATTR_DIRECTORY)) || (a_type==T_FILE && (ep->getINode()->rAttr()&ATTR_DIRECTORY))) { return nullptr; }
    return ep;
}
int Path::pathRemove(shared_ptr<File> a_file) const {
    shared_ptr<DEntry> ep = pathSearch(a_file);
    if(ep == nullptr) { return -1; }
    if((ep->getINode()->rAttr() & ATTR_DIRECTORY) && !ep->isEmpty()) { return -1; }
    ep->getINode()->nodRemove();
    return 0;
}
int Path::pathLink(shared_ptr<File> a_f1, const Path& a_newpath, shared_ptr<File> a_f2) const {
    shared_ptr<DEntry> dp1 = pathSearch(a_f1);
    shared_ptr<DEntry> dp2 = a_newpath.pathSearch(a_f2);
    if(dp1==nullptr || dp2==nullptr) {
        printf("can't find dir\n");
        return -1;
    }
    return dp1->getINode()->nodLink(dp2->getINode());
}
int Path::pathUnlink(shared_ptr<File> a_file) const {
    shared_ptr<DEntry> dp = pathSearch(a_file);
    if(dp == nullptr) { return -1; }
    if(dp->getINode()->nodUnlink() == -1) { return -1; }
    return pathRemove(a_file);
}
// shared_ptr<File> Path::pathOpen(int a_flags, shared_ptr<File> a_file) const {
//     DirEnt *ep = pathSearch(a_file);
//     if(ep == nullptr) { return nullptr; }
//     if((ep->attribute&ATTR_DIRECTORY) && ((a_flags&O_RDWR) || (a_flags&O_WRONLY))) {
//         printf("dir can't write\n");
//         ep->entRelse();
//         return nullptr;
//     }
//     if((a_flags&O_DIRECTORY) && !(ep->attribute&ATTR_DIRECTORY)) {
//         printf("it is not dir\n");
//         ep->entRelse();
//         return nullptr;
//     }
//     return make_shared<File>(ep, a_flags);
// }


