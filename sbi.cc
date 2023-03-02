#include "types.h"
#include "platform.h"
#include "rvcsr.h"

#define REPFIELD(name,bits) int u##name:bits; int s##name:bits; int wpri##name:bits; int m##name:bits;
#define BIT(x) (1ll<<x)
#define csrRead(reg, val) {asm volatile ("csrr %0, " #reg :"=r"(val):); }
#define csrWrite(reg, val) {asm volatile ("csrw "#reg", %0" :: "r"(val)); }
#define csrWritei(reg, val) {asm volatile ("csrw "#reg", %0" :: "i"(val)); }
#define csrSet(reg, val) {asm volatile ("csrs "#reg", %0" :: "r"(val)); }
#define csrSeti(reg, val) {asm volatile ("csrs "#reg", %0" :: "i"(val)); }
#define csrClear(reg, val) {asm volatile ("csrc "#reg", %0" :: "r"(val)); }
#define csrRW(val0,reg,val1) {asm volatile ("csrrw %0, "#reg", %1" :"=r"(val0):"r"(val1));}
#define csrSwap(reg,val) csrRW(val,reg,val)
#define ExecInst(inst) {asm volatile (#inst ::);}
struct MIE{
    REPFIELD(sie,1);
    REPFIELD(tie,1);
    REPFIELD(eie,1);
};
struct MIP{
    REPFIELD(sip,1);
    REPFIELD(tip,1);
    REPFIELD(eip,1);
};
FORCEDINLINE void MIEnable(){
    csrSeti(mstatus,BIT(csr::mstatus::fields::mie));
}
FORCEDINLINE bool isInterrupt(xlen_t mcause){
    return (mcause>>63)&1;
}
/*FORCEDINLINE inline void saveContext(){
    asm volatile ("csrrw t6,mscratch,t6"::);
    asm volatile (
    "sd ra, 0(t6)\n"
	"sd sp, 4(t6)\n"
	"sd gp, 8(t6)\n"
	"sd tp, 12(t6)\n"
	"sd t0, 16(t6)\n"
	"sd t1, 20(t6)\n"
	"sd t2, 24(t6)\n"
	"sd s0, 28(t6)\n"
	"sd s1, 32(t6)\n"
	"sd a0, 36(t6)\n"
	"sd a1, 40(t6)\n"
	"sd a2, 44(t6)\n"
	"sd a3, 48(t6)\n"
	"sd a4, 52(t6)\n"
	"sd a5, 56(t6)\n"
	"sd a6, 60(t6)\n"
	"sd a7, 64(t6)\n"
	"sd s2, 68(t6)\n"
	"sd s3, 72(t6)\n"
	"sd s4, 76(t6)\n"
	"sd s5, 80(t6)\n"
	"sd s6, 84(t6)\n"
	"sd s7, 88(t6)\n"
	"sd s8, 92(t6)\n"
	"sd s9, 96(t6)\n"
	"sd s10, 100(t6)\n"
	"sd s11, 104(t6)\n"
	"sd t3, 108(t6)\n"
	"sd t4, 112(t6)\n"
	"sd t5, 116(t6)\n"::); 
}*/
inline int Hart(){
    int rt; csrRead(mhartid,rt);
    return rt;
}
int plicClaim(){
    int hart=Hart();
    int irq=mmio<int>(platform::plic::claimOf(hart));
}
void plicComplete(int irq){
    int hart=Hart();
    mmio<int>(platform::plic::claimOf(hart))=irq;
}

void externalInterruptHandler(){
    int irq=plicClaim();
    if(irq==platform::uart0::irq){
        using namespace platform::uart0;
        while(mmio<volatile lsr>(reg(LSR)).rxnemp){
            mmio<volatile uint8_t>(reg(THR))=mmio<volatile uint8_t>(reg(RHR));
        }
    }
    plicComplete(irq);
}

__attribute__ ((interrupt ("machine")))void mtraphandler(){
    // saveContext();
    platform::uart0::puts("mtraphandler!");
    ptr_t mepc; csrRead(mepc,mepc);
    xlen_t mcause; csrRead(mcause,mcause);

    if(isInterrupt(mcause)){
        switch(mcause){
            using namespace csr::mcause;
            case usi: break;
            case ssi: break;
            case hsi: break;
            case msi: break;

            case uti: break;
            case sti: break;
            case hti: break;
            case mti: break;

            case uei: break;
            case sei: break;
            case hei: break;
            case mei: externalInterruptHandler();
        }
    } else {
        switch(mcause){
            using namespace csr::mcause;
            case uecall:break;
            case secall:break;
            default:
                break;
        }
    }
    // restoreContext();
    // ExecInst(mret);
}
void uartInit(){
    using namespace platform::uart0;
    mmio<uint8_t>(reg(IER))=0x00;
    // while(true){
        puts("Hello Uart\n");
    // }
}
void plicInit(){
    // int hart=Hart();
    int hart=0;
    xlen_t addr=platform::plic::priorityOf(platform::uart0::irq);
    mmio<word_t>(addr)=1;
    mmio<word_t>(platform::plic::enableOf(hart))=1<<platform::uart0::irq;
    uartInit();
}
void mtrap_test(){
    ExecInst(ecall);
}
extern "C" void sbi_init(){
    // csrWritei(mtvec,mtraphandler);
    {asm volatile ("csrw ""mtvec"", %0" :: "r"(mtraphandler));}
    using platform::uart0::puts;
    // uartInit();
    // plicInit();
    platform::uart0::puts("here\n");

    csrSet(mie,BIT(csr::mie::msie));
    // csrSet(mstatus,BIT(csr::mstatus::mie));
    // MIEnable();
    puts("here2\n");
    csrWrite(mepc,mtrap_test);
	ExecInst(mret);
    puts("here3");
}