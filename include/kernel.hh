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
        yield=124,
        times=153,
        uname=160,
        gettimeofday=169,
        getpid=172,
        getppid=173,
        clone=220,
        execve=221,
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
namespace kernel{
    constexpr int timerInterval=500000;

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

    struct KernelObjectsBuf{
        #define OBJBUF(type,name) uint8_t name##Buf[sizeof(type)]
        OBJBUF(alloc::HeapMgr,kHeapMgr);
        OBJBUF(alloc::PageMgr,kPageMgr);
        OBJBUF(vm::VMAR,kVMAR);
    };
    struct KernelGlobalObjs{
        vm::VMAR *vmar;
        alloc::PageMgr *pageMgr;
        alloc::HeapMgr *heapMgr;
        sched::Scheduler scheduler;
        proc::TaskManager taskMgr;
        proc::ProcManager procMgr;
        xlen_t ksatp;
    };
    // struct KernelTaskObjs{
    //     proc::Context ctx;
    //     sched::tid_t curTid,curPid;
    // };
    typedef proc::Task KernelTaskObjs;
    struct KernelHartObjs{
        KernelTaskObjs *curtask;
    };
    inline int readHartId(){register int hartid asm("tp"); return hartid;}
    void createKernelMapping(vm::VMAR &vmar);
    
}

extern kernel::KernelGlobalObjs kGlobObjs;
extern kernel::KernelHartObjs kHartObjs;
extern kernel::KernelInfo kInfo;

#endif