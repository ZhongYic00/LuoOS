#ifndef KERNEL_HH__
#define KERNEL_HH__

#include "common.h"
#include "rvcsr.hh"
#include "vm.hh"
#include "vm/pcache.hh"
#include "alloc.hh"
#include "sched.hh"
#include "proc.hh"

extern "C" void start_kernel(int hartid);
namespace syscall{
    void init();
    int sleep();
}
namespace sys
{
    enum syscalls{
        none,
        testexit,
        testyield,
        testwrite,
        testbio,
        testidle,
        testmount,
        testfatinit=7,
        getcwd=17,
        dup=23,
        dup3=24,
        mkdirat=34,
        unlinkat=35,
        symlinkat=36,
        linkat=37,
        umount2=39,
        mount=40,
        statfs=43,
        faccessat=48,
        chdir=49,
        fchdir=50,
        fchmod=52,
        fchmodat=53,
        fchownat=54,
        fchown=55,
        openat=56,
        close=57,
        pipe2=59,
        getdents64=61,
        lseek=62,
        read=63,
        write=64,
        sendfile=71,
        readlinkat=78,
        fstatat=79,
        fstat=80,
        sync=81,
        exit=93,
        exit_group=94,
        settidaddress=96,
        nanosleep=101,
        yield=124,
        kill=129,
        tkill=130,
        sigaction=134,
        sigprocmask=135,
        sigreturn=139,
        reboot=142,
        setgid=144,
        setuid=146,
        getresuid=148,
        getresgid=150,
        setpgid=154,
        getpgid=155,
        getsid=156,
        setsid=157,
        getgroups=158,
        setgroups=159,
        times=153,
        uname=160,
        getrlimit=163,
        setrlimit=164,
        umask=166,
        gettimeofday=169,
        getpid=172,
        getppid=173,
        getuid=174,
        geteuid=175,
        getgid=176,
        getegid=177,
        gettid=178,
        brk=214,
        munmap=215,
        clone=220,
        execve=221,
        mmap=222,
        wait=260,
        syncfs=267,
        nSyscalls,
    };
    enum statcode{
        ok=0, err=-1
    };
    static inline xlen_t syscall6(xlen_t id, xlen_t arg0, xlen_t arg1, xlen_t arg2, xlen_t arg3, xlen_t arg4, xlen_t arg5){
        register xlen_t a0 asm("a0") = arg0;
        register xlen_t a1 asm("a1") = arg1;
        register xlen_t a2 asm("a2") = arg2;
        register xlen_t a3 asm("a3") = arg3;
        register xlen_t a4 asm("a4") = arg4;
        register xlen_t a5 asm("a5") = arg5;
        register long a7 asm("a7") = id;
        asm volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"
                (a5), "r"(a7));
        return a0;
    }

    inline int syscall(int id){
        register int a7 asm("a7")=id;
        ExecInst(ecall);
        register int a0 asm("a0");
        return a0;
    }
    inline int syscall1(int id,xlen_t arg0){
        return syscall6(id,arg0,0,0,0,0,0);
    }
    inline int syscall2(int id,xlen_t arg0,xlen_t arg1){
        return syscall6(id,arg0,arg1,0,0,0,0);
    }
    inline int syscall3(int id,xlen_t arg0,xlen_t arg1,xlen_t arg2){
        return syscall6(id,arg0,arg1,arg2,0,0,0);
    }
    
} // namespace sy
namespace kernel {
    constexpr int timerInterval=100000;
    constexpr long INTERVAL = 390000000 / 100;
    constexpr long CLK_FREQ = 8900000;
    constexpr int NMAXSLEEP = 32;

    typedef proc::Task KernelTaskObjs;

    struct KernelInfo{
        using segment_t=vm::segment_t;
        struct KSegments{
            segment_t dev,text,rodata,data,kstack,bss,frames,ramdisk;
        }segments;
        struct KVMOs{
            Arc<vm::VMO> dev,text,rodata,data,kstack,bss,frames,ramdisk;
        }vmos;
    };
    struct KernelGlobalObjs{
        mutex::LockedObject<alloc::HeapMgrGrowable,spinlock<false>> heapMgr;
        mutex::LockedObject<alloc::PageMgr> pageMgr;
        mutex::LockedObject<vm::VMAR> vmar;
        mutex::LockedObject<sched::Scheduler,spinlock<false>> scheduler;
        mutex::LockedObject<proc::TaskManager,spinlock<false>> taskMgr;
        mutex::LockedObject<proc::ProcManager,spinlock<false>> procMgr;
        vm::PageCacheMgr pageCache;
        xlen_t ksatp;
        xlen_t prevsatp;
        KernelGlobalObjs();
    };
    struct KernelHartObjs{
        KernelTaskObjs *curtask;
        uint g_ticks;
        proc::SleepingTask sleep_tasks[NMAXSLEEP];
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
    FORCEDINLINE inline int readHartId(){register int hartid asm("tp"); return hartid;}
    constexpr tid_t kthreadIdBase=0x80000000;
    inline int threadId(){return kthreadIdBase+readHartId();}
    void createKernelMapping(vm::VMAR &vmar);
}
extern kernel::KernelGlobalObjs *kGlobObjs;
extern kernel::KernelHartObjs kHartObjs[8];
FORCEDINLINE
inline kernel::KernelHartObjs& kHartObj(){return kHartObjs[kernel::readHartId()];}
extern kernel::KernelInfo kInfo;

#endif