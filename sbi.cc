#include "types.h"
#include "platform.h"
#include "klib.hh"
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
    return irq;
}
void plicComplete(int irq){
    int hart=Hart();
    mmio<int>(platform::plic::claimOf(hart))=irq;
}

void externalInterruptHandler(){
    int irq=plicClaim();
    // printf("externalInterruptHandler(irq=%d)\n",irq);
    static int levelcnt=0;
    levelcnt++;
    if(irq==platform::uart0::irq){
        using namespace platform::uart0;
        int cnt=0;
        while(mmio<volatile lsr>(reg(LSR)).rxnemp){
            char c=mmio<volatile uint8_t>(reg(RHR));
            printf("%c",c);
            cnt++;
        }
        // for(char c;mmio<volatile lsr>(reg(LSR)).txidle && (c=IO::next())>0;)mmio<volatile uint8_t>(reg(THR))=c;
        if(cnt)printf("&%d|%d^",cnt,levelcnt);
    }
    plicComplete(irq);
    levelcnt--;
    // printf("externalInterruptHandler over");
}
extern "C" __attribute__((interrupt("machine"))) void mtraphandler(){
    ptr_t mepc; csrRead(mepc,mepc);
    xlen_t mcause; csrRead(mcause,mcause);
    // printf("mtraphandler cause=[%d]%d mepc=%lx\n",isInterrupt(mcause),mcause<<1>>1,mepc);

    if(isInterrupt(mcause)){
        switch(mcause<<1>>1){
            using namespace csr::mcause;
            // case usi: break;
            // case ssi: break;
            // case hsi: break;
            // case msi: break;

            // case uti: break;
            // case sti: break;
            // case hti: break;
            // case mti: break;

            // case uei: break;
            // case sei: break;
            // case hei: break;
            case mei: externalInterruptHandler();break;
            default:
                halt();
        }
    } else {
        switch(mcause){
            using namespace csr::mcause;
            case uecall:break;
            case secall:break;
            case storeAccessFault:break;
            default:
                printf("exception\n");
                halt();
        }
        csrWrite(mepc,mepc+4);
    }
    // printf("mtraphandler over\n");
}
void uartIntTest(){
    using namespace platform::uart0::nonblocking;
    for(int i=1024;i;i--)putc('_');
    // 0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg
}
void uartInit(){
    using namespace platform::uart0;
    auto &ier=mmio<volatile uint8_t>(reg(IER));
    ier=0x00;
    puts("Hello Uart\n");
    auto &lcr=mmio<volatile uint8_t>(reg(LCR));
    lcr=lcr|(1<<7);
    mmio<volatile uint8_t>(reg(DLL))=0x03;
    mmio<volatile uint8_t>(reg(DLM))=0x00;
    lcr=3;
    mmio<volatile uint8_t>(reg(FCR))=0x7|(0x3<<6);
    ier=0x03;
    // puts=IO::_nonblockingputs;
}
void plicInit(){
    // int hart=Hart();
    using namespace platform::plic;
    int hart=0;
    xlen_t addr=priorityOf(platform::uart0::irq);
    mmio<word_t>(addr)=1;
    mmio<word_t>(enableOf(hart))=1<<platform::uart0::irq;
    mmio<word_t>(thresholdOf(hart))=0;
    uartInit();
}
void mtrap_test(){
    *(int *)0x00000000 = 100;
    for(int k=2;k;k--);
    return;
}
extern "C" void sbi_init(){
//     asm volatile (
// "csrw pmpaddr0, %1\n\t"
// "csrw pmpcfg0, %0\n\t"
// : : "r" (0xf), "r" (0x3fffffffffffffull) :);
    // csrWritei(mtvec,mtraphandler);
    // using platform::uart0::blocking::puts;
    // uartInit();
    puts=IO::_blockingputs;
    plicInit();
    puts("plic init over\n");

    {asm volatile ("csrw ""mtvec"", %0" :: "r"(mtraphandler));}
    csrSet(mie,BIT(csr::mie::meie));
    csrSet(mstatus,BIT(csr::mstatus::mie));
    // puts("here2\n");
    mtrap_test();
    puts("mtrap test over");
    uartIntTest();
}