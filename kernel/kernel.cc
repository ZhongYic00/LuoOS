#include "../include/klib.hh"
#include "../include/rvcsr.hh"
#include "../include/sbi.hh"
#include "../include/kernel.hh"
#include "../include/vm.hh"
#define TIMER_INTERVAL 5000000

static sys::context ctx;
static hook_t hooks[10];
__attribute__((section("pagetable")))
xlen_t kernelPageTableRootPage[4096];
using vm::pgtbl_t;
pgtbl_t kernelPageTableRoot=reinterpret_cast<pgtbl_t>(kernelPageTableRootPage);

void timerInterruptHandler(){
    int hart=sbi::readHartId();
    static int cnt=0;
    xlen_t time;
    csrRead(time,time);
    auto mtime=mmio<volatile xlen_t>(platform::clint::mtime);
    volatile xlen_t& mtimecmp=mmio<volatile xlen_t>(platform::clint::mtimecmpOf(hart));
    printf("S-Mode timer Interrupt[%d]::%ld %ld<%ld\n",cnt++,time,mtimecmp,mtime);
    mtimecmp+=TIMER_INTERVAL;
    for(auto hook: hooks)if(hook)hook();
    sbi::resetXTIE();
}
int none(){
    return 0;
}
constexpr static syscall_t syscallPtrs[]={none,none};
void uecallHandler(){
    register int ecallId asm("a7");
    xlen_t &rtval=ctx.x(10);
    printf("uecall [%d]\n",ecallId);
    using namespace sys;
    if(ecallId<nSyscalls)rtval=syscallPtrs[ecallId]();
    else rtval=1;
}

extern "C" void straphandler(){
    ptr_t sepc; csrRead(sepc,sepc);
    xlen_t scause; csrRead(scause,scause);
    // printf("straphandler cause=[%d]%d mepc=%lx\n",isInterrupt(mcause),mcause<<1>>1,mepc);

    if(csr::mcause::isInterrupt(scause)){
        switch(scause<<1>>1){
            using namespace csr::mcause;
            // case usi: break;
            // case ssi: break;
            // case hsi: break;
            // case msi: break;

            // case uti: break;
            case sti: timerInterruptHandler(); break;
            // case hti: break;
            // case mti: break;

            // case uei: break;
            // case sei: break;
            // case hei: break;
            // case mei: break;
            default:
                halt();
        }
    } else {
        switch(scause){
            using namespace csr::mcause;
            case uecall:uecallHandler();break;
            // case secall:break;
            case storeAccessFault:break;
            default:
                printf("exception\n");
                halt();
        }
        csrWrite(sepc,sepc+4);
    }
    // printf("mtraphandler over\n");
}
extern "C" __attribute__((naked)) void strapwrapper(){
    csrSwap(sscratch,t6);
    saveContext();
    straphandler();
    restoreContext();
    csrSwap(sscratch,t6);
    ExecInst(sret);
}
static void timerInit(){
    int hart=sbi::readHartId();
    printf("hart=%d\n",hart);
    mmio<xlen_t>(platform::clint::mtimecmpOf(hart))=mmio<xlen_t>(platform::clint::mtime)+TIMER_INTERVAL;
    csrSet(sie,BIT(csr::mie::stie));
}

__attribute__((nacked, section("stub")))
static void stub(){
    csrWrite(satp,kernelPageTableRoot);
    ExecInst(sfence.vma);
    ExecInst(j strapwrapper);
}

// __attribute__((section("init")))
static void memInit(){
    csr::satp satp;
    satp.mode=8;
    satp.asid=0;
    satp.ppn=reinterpret_cast<xlen_t>(kernelPageTableRoot)>>12;
    kernelPageTableRoot[0].ppn1=0x0;
    kernelPageTableRoot[0].perm=0xcf;
    kernelPageTableRoot[1].ppn1=0x100;
    kernelPageTableRoot[1].perm=0xcf;
    kernelPageTableRoot[2].ppn2=0x1;
    kernelPageTableRoot[2].perm=0xcf;
    csrWrite(satp,satp.value());
    ExecInst(sfence.vma);

}
extern void program0();
extern "C" //__attribute__((section("init")))
void start_kernel(){
    puts=IO::_blockingputs;
    memInit();
    csrWrite(sscratch,ctx.gpr);
    csrWrite(stvec,strapwrapper);
    csrSet(sstatus,BIT(csr::mstatus::sie));
    csrSet(sie,BIT(csr::mie::ssie));
    timerInit();
    // halt();
    // while(true);
    for(int i=0;i<10;i++)
        printf("%d:Hello RVOS!\n",i);
    csrClear(sstatus,1l<<csr::mstatus::spp);
    halt();
    // csrWrite(sepc,program0);
    // ExecInst(sret);
}