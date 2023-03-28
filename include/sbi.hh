#include "rvcsr.hh"

namespace sbi
{
    enum ecalls{
        none,
        hart,
        xtie,
        xsie,
    };
    inline int ecall(int ecallId){
        register int a7 asm("a7")=ecallId;
        ExecInst(ecall);
        register int a0 asm("a0");
        return a0;
    }
    inline int readHartId(){return ecall(ecalls::hart);}
    inline int resetXTIE(){return ecall(ecalls::xtie);}
    inline int resetXSIE(){return ecall(ecalls::xsie);}
} // namespace sbi
