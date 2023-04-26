#ifndef KERNEL_HH__
#define KERNEL_HH__

#include "common.h"
#include "rvcsr.hh"
#include "vm.hh"
#include "alloc.hh"
#include "sched.hh"
#include "proc.hh"

extern "C" void start_kernel();
namespace sys
{
    enum syscalls{
        none,
        testexit,
        nSyscalls,
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
    inline int syscall1(int id,int arg0){
        return syscall6(id,arg0,0,0,0,0,0);
    }
    
} // namespace sy
namespace kernel{
    constexpr int timerInterval=5000000;

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
        OBJBUF(vm::PageTable,kPageTable);
    };
    struct KernelGlobalObjs{
        sched::Scheduler scheduler;
    };
    struct KernelLocalObjs{
        proc::Context ctx;
    };
    
    void createKernelMapping(vm::PageTable &pageTable);
    
}

extern alloc::PageMgr *kernelPmgr;
extern xlen_t ksatp,usatp;
extern kernel::KernelGlobalObjs kGlobObjs;
extern kernel::KernelLocalObjs kLocObjs;

#endif