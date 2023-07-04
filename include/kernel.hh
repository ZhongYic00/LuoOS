#ifndef KERNEL_HH__
#define KERNEL_HH__

#include "common.h"
#include "rvcsr.hh"
#include "vm.hh"
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
        linkat=37,
        umount2=39,
        mount=40,
        chdir=49,
        openat=56,
        close=57,
        pipe2=59,
        getdents64=61,
        read=63,
        write=64,
        fstat=80,
        exit=93,
        nanosleep=101,
        yield=124,
        reboot=142,
        times=153,
        uname=160,
        gettimeofday=169,
        getpid=172,
        getppid=173,
        brk=214,
        munmap=215,
        clone=220,
        execve=221,
        mmap=222,
        wait=260,
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
            segment_t dev;
            segment_t text;
            segment_t rodata;
            segment_t data;
            segment_t kstack;
            segment_t bss;
            segment_t frames;
        }segments;
    };
    struct KernelGlobalObjs{
        mutex::LockedObject<alloc::HeapMgrGrowable,spinlock<false>> heapMgr;
        mutex::LockedObject<alloc::PageMgr> pageMgr;
        mutex::LockedObject<vm::VMAR> vmar;
        mutex::LockedObject<sched::Scheduler,spinlock<false>> scheduler;
        mutex::LockedObject<proc::TaskManager,spinlock<false>> taskMgr;
        mutex::LockedObject<proc::ProcManager,spinlock<false>> procMgr;
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
    class TimeSpec {
        private:
            time_t m_tv_sec;  /* 秒 */
            long m_tv_nsec; /* 纳秒, 范围在0~999999999 */
        public:
            TimeSpec():m_tv_sec(0), m_tv_nsec(0) {}
            TimeSpec(time_t a_tv_sec, long a_tv_nsec):m_tv_sec(a_tv_sec), m_tv_nsec(a_tv_nsec) {}
            inline time_t tvSec() { return m_tv_sec; }
            inline time_t tvNSec() { return m_tv_nsec; }
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