#include "common.h"
#include "kernel.hh"
#include "proc.hh"
#include "fs/ramfs.hh"
#include "fs.hh"
#include <EASTL/chrono.h>
#include <linux/rtc.h>

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
static unsigned short days[4][12] =
{
    {   0,  31,  60,  91, 121, 152, 182, 213, 244, 274, 305, 335},
    { 366, 397, 425, 456, 486, 517, 547, 578, 609, 639, 670, 700},
    { 731, 762, 790, 821, 851, 882, 912, 943, 974,1004,1035,1065},
    {1096,1127,1155,1186,1216,1247,1277,1308,1339,1369,1400,1430},
};
class RTCFile:public ramfs::INode{
public:
    RTCFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
    int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
        auto dst=(uint8_t*)addr;
        int rdbytes=0,off=uoff;
        auto cur=eastl::chrono::system_clock::now();
        auto timeSinceEpoch=cur.time_since_epoch().count();
        auto bytes=sizeof(timeSinceEpoch);
        if(off<bytes){
            memcpy(dst,&timeSinceEpoch,bytes);
            dst+=bytes;
            rdbytes+=bytes;
        }
        if(off>0)off-=klib::min(bytes,off);
        return rdbytes;
    }
    mode_t rMode() const override {return S_IFCHR;}
    int ioctl(uint64_t req,addr_t arg) override{
        switch(req){
            case RTC_RD_TIME:{
                auto epoch=eastl::chrono::system_clock::now().time_since_epoch().count();
                struct rtc_time time;
                time.tm_sec=epoch%60;epoch/=60;
                time.tm_min=epoch%60;epoch/=60;
                time.tm_hour=epoch%24;epoch/=24;

                auto years=epoch/(365*4+1)*4; epoch%=365*4+1;
                unsigned int year;
                for (year=3; year>0; year--)
                    if (epoch >= days[year][0])
                        break;
                unsigned int month;
                for (month=11; month>0; month--)
                    if (epoch >= days[year][month])
                        break;
                time.tm_year=years+year;
                time.tm_mon=month+1;
                time.tm_mday=epoch-days[year][month]+1;
                kHartObj().curtask->getProcess()->vmar[arg]<<time;
                return 0;
            }break;
            default:
                return -EINVAL;
        }
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
        ep->setMntPoint();
        Log(info,"fat initialize ok");
        auto procfs=make_shared<ramfs::FileSystem>();
        {
            auto root=procfs->getSpBlk()->getRoot();
            auto mounts=dynamic_cast<ramfs::SuperBlock*>(procfs->getSpBlk().get())->mknod<MountsFile>();
            root->getINode()->link("mounts",mounts);
            auto self=root->entCreate(root,"self",S_IFDIR).value();
            auto mountsinfo=dynamic_cast<ramfs::SuperBlock*>(procfs->getSpBlk().get())->mknod<MountsInfoFile>();
            self->getINode()->link("mountinfo",mountsinfo);
            auto meminfo=dynamic_cast<ramfs::SuperBlock*>(procfs->getSpBlk().get())->mknod<MemInfoFile>();
            root->getINode()->link("meminfo",meminfo);
            Log(warning,"%d",mounts->rINo());
            Path("/proc").mount(procfs);
        }
        auto devfs=make_shared<ramfs::FileSystem>();
        {
            auto root=devfs->getSpBlk()->getRoot();
            auto misc=root->entCreate(root,"misc",S_IFDIR).value();
            auto rtc=dynamic_cast<ramfs::SuperBlock*>(devfs->getSpBlk().get())->mknod<RTCFile>();
            misc->getINode()->link("rtc",rtc);
            Path("/dev").mount(devfs);
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
