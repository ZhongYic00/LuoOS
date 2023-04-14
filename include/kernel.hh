#include "common.h"
#include "rvcsr.hh"

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
    
    struct context
    {
        xlen_t gpr[30];
        constexpr xlen_t& x(int r){return gpr[r-1];}
    };
    
} // namespace sy
