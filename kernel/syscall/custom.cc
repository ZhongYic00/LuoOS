#include "common.h"
#include "kernel.hh"
#include "proc.hh"
#include "fs/ramfs.hh"
#include "fs.hh"

extern unordered_map<string, shared_ptr<fs::FileSystem>> mnt_table;

class MountsFile:public ramfs::INode{
public:
    MountsFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
    int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
        auto dst=(uint8_t*)addr;
        int rdbytes=0,off=uoff;
        for(auto item:mnt_table){
            auto line=item.first+" on "+item.first+" type "+item.second->rFSType()+"(rw)\n";
            int bytes=klib::min(len,line.size())-off;
            if(off<bytes){
                memcpy(dst,line.c_str()+off,bytes);
                dst+=bytes;
                rdbytes+=bytes;
            }
            if(off>0)off-=klib::min(bytes,off);
        }
        return rdbytes;
    }
};
class MountsInfoFile:public ramfs::INode{
public:
    MountsInfoFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
    int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
        auto dst=(uint8_t*)addr;
        int rdbytes=0,off=uoff;
        for(auto item:mnt_table){
            auto line=klib::format("%d %d %d:0 / %s rw,noatime - %s none rw,errors=continue",
                eastl::hash<string>()(item.first),  // mount-id
                0,  // parent id
                1,  // st_dev,
                item.first, // mount point
                item.second->rFSType()  // fs type
            );
            int bytes=klib::min(len,line.size())-off;
            if(off<bytes){
                memcpy(dst,line.c_str()+off,bytes);
                dst+=bytes;
                rdbytes+=bytes;
            }
            if(off>0)off-=klib::min(bytes,off);
        }
        return rdbytes;
    }
};

class MemInfoFile:public ramfs::INode{
public:
    MemInfoFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
    int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
        auto dst=(uint8_t*)addr;
        int rdbytes=0,off=uoff;
        for(auto item:mnt_table){
            auto line=item.first+" on "+item.first+" type "+item.second->rFSType()+"(rw)\n";
            int bytes=klib::min(len,line.size())-off;
            if(off<bytes){
                memcpy(dst,line.c_str()+off,bytes);
                dst+=bytes;
                rdbytes+=bytes;
            }
            if(off>0)off-=klib::min(bytes,off);
        }
        return rdbytes;
    }
};

namespace syscall
{
    using namespace fs;
    using sys::statcode;
    sysrt_t testFATInit() {
        // @todo: 处理/proc/mounts
        Log(info, "initializing fat\n");
        int init = fs::rootFSInit();
        if(init != 0) { panic("fat init failed\n"); }
        auto curproc = kHartObj().curtask->getProcess();
        // curproc->cwd = fs::entEnter("/");
        curproc->cwd = Path("/").pathSearch();
        curproc->files[proc::FdCwd] = make_shared<File>(curproc->cwd,0);
        // Path("/proc").pathRemove();
        shared_ptr<DEntry> ep = Path("/proc").pathCreate(T_DIR, 0);
        if(ep == nullptr) { panic("create /dev failed\n"); }
        ep = Path("/proc/mounts").pathCreate(T_FILE, 0);
        if(ep == nullptr) { panic("create /proc/mounts failed\n"); }
        auto file = make_shared<File>(ep, O_RDWR);
        const char content[] = "fat32 /";
        file->write(ByteArray((uint8*)content, strlen(content)+1));
        Log(info,"fat initialize ok");
        auto procfs=make_shared<ramfs::FileSystem>();
        {
            auto root=procfs->getSpBlk()->getRoot();
            auto mounts=dynamic_cast<ramfs::SuperBlock*>(procfs->getSpBlk().get())->mknod<MountsFile>();
            root->getINode()->link("mounts",mounts);
            auto mountsinfo=dynamic_cast<ramfs::SuperBlock*>(procfs->getSpBlk().get())->mknod<MountsInfoFile>();
            root->getINode()->link("mountsinfo",mountsinfo);
            auto meminfo=dynamic_cast<ramfs::SuperBlock*>(procfs->getSpBlk().get())->mknod<MemInfoFile>();
            root->getINode()->link("meminfo",meminfo);
            Log(warning,"%d",mounts->rINo());
            Path("/proc").mount(procfs);
        }
        {
            auto ep=Path("/proc/mounts").pathSearch();
            auto name=ep->rName().c_str();
            auto nod=ep->getINode();
            File mounts(ep,O_RDONLY);
            ByteArray buf(100);
            mounts.read(buf,5);
            printf("mounts:\n%s\n",buf.buff);
            delete buf.buff;
        }

        return statcode::ok;
    }
} // namespace syscall
