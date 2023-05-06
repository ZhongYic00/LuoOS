#include "../include/sbi.hh"
#include "../include/platform.h"
#include "../include/klib.hh"
#include "../include/kernel.hh"
#include "../include/rvcsr.hh"


#define TIMER_INTERVAL 5000000

static xlen_t gpr[30];

int plicClaim(){
    int hart=csr::hart();
    int irq=mmio<int>(platform::plic::claimOf(hart));
    return irq;
}
void plicComplete(int irq){
    int hart=csr::hart();
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
            // printf("%c",c);
            cnt++;
        }
        // for(char c;mmio<volatile lsr>(reg(LSR)).txidle && (c=IO::next())>0;)mmio<volatile uint8_t>(reg(THR))=c;
        if(cnt)printf("&%d|%d^",cnt,levelcnt);
    }
    plicComplete(irq);
    levelcnt--;
}
static void timerInterruptHandler(){
    int hart=csr::hart();
    static int cnt=0;
    xlen_t time;csrRead(time,time);
    printf("timer Interrupt[%d]::%ld\n",cnt++,time);
    csrSet(mip,BIT(csr::mip::stip));
    csrClear(mie,BIT(csr::mie::mtie));
}
static void secallHandler(){
    register int ecallId asm("a7");
    xlen_t &rtval=gpr[9];
    printf("secall [%d]\n",ecallId);
    switch(ecallId){
    using namespace sbi;
        case hart: csrRead(mhartid,rtval);break;
        case xtie: csrSet(mie,BIT(csr::mie::mtie));csrClear(mip,BIT(csr::mip::stip));rtval=0;break;
        case xsie: csrSet(mie,BIT(csr::mie::msie));csrClear(mip,BIT(csr::mip::ssip));rtval=0;break;
        default: rtval=1;
    }
}
// extern "C" __attribute__((interrupt("machine"))) void mtraphandler(){
extern "C" void mtraphandler(){
    ptr_t mepc; csrRead(mepc,mepc);
    xlen_t mcause; csrRead(mcause,mcause);
    xlen_t mtval; csrRead(mtval,mtval);
    // csr::currentMode();
    // printf("mtraphandler cause=[%d]%d mepc=%lx\n",isInterrupt(mcause),mcause<<1>>1,mepc);

    if(csr::mcause::isInterrupt(mcause)){
        switch(mcause<<1>>1){
            using namespace csr::mcause;
            // case usi: break;
            // case ssi: break;
            // case hsi: break;
            // case msi: break;

            // case uti: break;
            // case sti: break;
            // case hti: break;
            case mti: timerInterruptHandler();break;

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
            case secall:secallHandler();csrWrite(mepc,mepc+4);break;
            case storeAccessFault:csrWrite(mepc,mepc+4);break;
            default:
                printf("exception%d %p %p\n",mcause,mepc,mtval);
                csrWrite(mepc,mepc+4);
                // halt();
        }
    }
    // printf("mtraphandler over\n");
}
xlen_t mstack;
extern "C" __attribute__((naked)) void mtrapwrapper(){
    csrSwap(mscratch,t6);
    saveContext();
    register xlen_t sp asm("sp")=mstack;
    mtraphandler();
    restoreContext();
    csrSwap(mscratch,t6);
    ExecInst(mret);
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
    ier=0x01;
    // puts=IO::_nonblockingputs;
}
void plicInit(){
    // int hart=csr::hart();
    using namespace platform::plic;
    int hart=csr::hart();
    xlen_t addr=priorityOf(platform::uart0::irq);
    mmio<word_t>(addr)=1;
    mmio<word_t>(enableOf(hart))=1<<platform::uart0::irq;
    mmio<word_t>(thresholdOf(hart))=0;
    uartInit();
}
void timerInit(){
    int hart=csr::hart();
    mmio<xlen_t>(platform::clint::mtimecmpOf(hart))=TIMER_INTERVAL;
    csrSet(mie,BIT(csr::mie::mtie));
}
void mtrap_test(){
    *(int *)0x00000000 = 100;
    puts("mtrap test over");
    for(int k=2;k;k--);
    return;
}
extern "C" void sbi_init(){
//     asm volatile (
// "csrw pmpaddr0, %1\n\t"
// "csrw pmpcfg0, %0\n\t"
// : : "r" (0xf), "r" (0x3fffffffffffffull) :);
    // csrWritei(mtvec,mtraphandler);
    register xlen_t sp asm("sp");
    mstack=sp;
    puts=IO::_blockingputs;
    plicInit();
    puts("plic init over\n");

    csrWrite(mscratch,gpr);
    {asm volatile ("csrw ""mtvec"", %0" :: "r"(mtrapwrapper));}
    csrSet(mie,BIT(csr::mie::meie));
    // csrSet(mstatus,BIT(csr::mstatus::mie));
    // mtrap_test();
    uartIntTest();

    // timerInit();
    csrSet(mideleg,BIT(csr::mie::stie)|BIT(csr::mie::ssie));
    csrSet(medeleg,BIT(csr::mcause::uecall));
    csrSet(mie,BIT(csr::mie::mtie));
    csrSet(mstatus,1l<<csr::mstatus::mpp);
    csrWrite(mepc,0x80200000l);
    csrSet(mstatus,BIT(csr::mstatus::mpie));
    ExecInst(mret);
}