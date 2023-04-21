#ifndef RVCSR_H
#define RVCSR_H

#include "common.h"

#define REPFIELD(name) u##name, s##name,h##name,m##name,
#define csrRead(reg, val) {asm volatile ("csrr %0, " #reg :"=r"(val):); }
#define csrWrite(reg, val) {asm volatile ("csrw "#reg", %0" :: "r"(val)); }
#define csrWritei(reg, val) {asm volatile ("csrw "#reg", %0" :: "i"(val)); }
#define csrSet(reg, val) {asm volatile ("csrs "#reg", %0" :: "r"(val)); }
#define csrSeti(reg, val) {asm volatile ("csrs "#reg", %0" :: "i"(val)); }
#define csrClear(reg, val) {asm volatile ("csrc "#reg", %0" :: "r"(val)); }
#define csrRW(val0,reg,val1) {asm volatile ("csrrw %0, "#reg", %1" :"=r"(val0):"r"(val1));}
#define csrSwap(csr,gpr) {__asm__("csrrw "#gpr","#csr","#gpr);}
#define ExecInst(inst) {asm volatile (#inst ::);}

namespace csr
{
    namespace mie
    {
        enum fields{
            REPFIELD(sie)
            REPFIELD(tie)
            REPFIELD(eie)
        };
    } // namespace mie
    namespace mip
    {
        enum fields{
            REPFIELD(sip)
            REPFIELD(tip)
            REPFIELD(eip)
        };
    } // namespace mip
    
    namespace mcause{
        enum interrupts{
            REPFIELD(si)
            REPFIELD(ti)
            REPFIELD(ei)
        };
        enum exceptions{
            breakpoint=3,
            storeAccessFault=7,
            uecall=8,
            secall=9,
        };
        constexpr bool isInterrupt(xlen_t mcause){return (mcause>>63)&1;}
    }
    namespace mstatus{
        enum fields{
            REPFIELD(ie)
            REPFIELD(pie)
            spp,mpp=11,
            sum=18,
        };
    }
    struct satp{
        xlen_t ppn:44;
        xlen_t asid:16;
        xlen_t mode:4;
        inline xlen_t value(){
            return *reinterpret_cast<xlen_t*>(this);
        }
    };
    inline int hart(){
        int rt; csrRead(mhartid,rt);
        return rt;
    }
} // namespace csr

#endif