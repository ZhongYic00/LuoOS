#ifndef KERNEL_HH__
#define KERNEL_HH__

#include "common.h"
#include "rvcsr.hh"
#include "vm.hh"
#include "vm/pcache.hh"
#include "alloc.hh"
#include "sched.hh"
#include "proc.hh"
#include "sys.hh"

extern "C" void start_kernel(int hartid);
namespace timeservice{ class Timer; }
namespace kernel {
    constexpr int nameLen=65;

    typedef proc::Task KernelTaskObjs;

    void sleep();
    void yield();

    struct KernelInfo{
        using segment_t=vm::segment_t;
        struct KSegments{
            segment_t dev,text,rodata,data,vdso,kstack,bss,frames,ramdisk;
            segment_t mapper;
        }segments;
        struct KVMOs{
            Arc<vm::VMO> dev,text,rodata,data,vdso,kstack,bss,frames,ramdisk;
        }vmos;
        struct utsname{
            const char sysname[nameLen]="LuoOS";
            const char nodename[nameLen]="0";
            const char release[nameLen]="5.0.0";
            const char version[nameLen]="#1";
            const char machine[nameLen]="RISCV-64";
        }uts;
    };
    struct KernelGlobalObjs{
        mutex::LockedObject<alloc::HeapMgrGrowable,spinlock<false>> heapMgr;
        mutex::LockedObject<alloc::PageMgr> pageMgr;
        mutex::LockedObject<vm::VMAR> vmar;
        mutex::LockedObject<sched::Scheduler,spinlock<false>> scheduler;
        mutex::LockedObject<proc::TaskManager,spinlock<false>> taskMgr;
        mutex::LockedObject<proc::ProcManager,spinlock<false>> procMgr;
        vm::PageCacheMgr pageCache;
        unordered_map<addr_t,condition_variable::condition_variable> futexes;
        xlen_t ksatp;
        xlen_t prevsatp;
        KernelGlobalObjs();
    };
    struct KernelHartObjs{
        KernelTaskObjs *curtask;
        timeservice::Timer *timer,*vtimer;
    };
    typedef mutex::ObjectGuard<kernel::KernelGlobalObjs> KernelGlobalObjsRef;
    struct KernelObjectsBuf{
        #define OBJBUF(type,name) uint8_t name##Buf[sizeof(type)]
        OBJBUF(KernelGlobalObjs,kGlobObjs);
    };
    struct UtSName {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    };
    constexpr tid_t kthreadIdBase=0x80000000;
    inline int threadId(){return kthreadIdBase+readHartId();}
    void createKernelMapping(vm::VMAR &vmar);
}
FORCEDINLINE
inline int readHartId(){int rt;regRead(tp,rt);return rt;}
extern kernel::KernelGlobalObjs *kGlobObjs;
extern kernel::KernelHartObjs kHartObjs[8];
FORCEDINLINE
inline kernel::KernelHartObjs& kHartObj(){return kHartObjs[readHartId()];}
extern kernel::KernelInfo kInfo;

#endif