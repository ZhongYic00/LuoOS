#include "common.h"
#include "rvcsr.hh"
#include "vm.hh"
#include "alloc.hh"

extern "C" void start_kernel();
namespace sys
{
    enum syscalls{
        none,
        one,
        nSyscalls,
    };
    inline int syscall(int id){
        register int a7 asm("a7")=id;
        ExecInst(ecall);
        register int a0 asm("a0");
        return a0;
    }
    
} // namespace sy
namespace kernel{
    struct Context
    {
        xlen_t gpr[30];
        constexpr xlen_t& x(int r){return gpr[r-1];}
        ptr_t stack;
        xlen_t pc;
    };
    constexpr int timerInterval=5000000;

    struct KernelInfo{
        using segment_t=vm::segment_t;
        struct KSegments{
            segment_t dev;
            segment_t text;
            segment_t rodata;
            segment_t data;
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
    
}
extern kernel::Context ctx;
extern alloc::PageMgr *kernelPmgr;