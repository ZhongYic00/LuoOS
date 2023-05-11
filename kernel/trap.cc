
#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"

extern void schedule();
static hook_t hooks[10]={schedule};

void timerInterruptHandler(){
    int hart=sbi::readHartId();
    static int cnt=0;
    xlen_t time;
    csrRead(time,time);
    auto mtime=mmio<volatile xlen_t>(platform::clint::mtime);
    volatile xlen_t& mtimecmp=mmio<volatile xlen_t>(platform::clint::mtimecmpOf(hart));
    printf("S-Mode timer Interrupt[%d]::%ld %ld<%ld\n",cnt++,time,mtimecmp,mtime);
    mtimecmp+=kernel::timerInterval;
    for(auto hook: hooks)if(hook)hook();
    sbi::resetXTIE();
}
extern syscall_t syscallPtrs[];
void uecallHandler(){
    register int ecallId asm("a7");
    xlen_t &rtval=kHartObjs.curtask->ctx.x(10);
    kHartObjs.curtask->ctx.pc+=4;
    printf("uecall [%d]\n",ecallId);
    using namespace sys;
    if(ecallId<nSyscalls)rtval=syscallPtrs[ecallId]();
    else rtval=1;
}

extern "C" void straphandler(){
    ptr_t sepc; csrRead(sepc,sepc);
    xlen_t scause; csrRead(scause,scause);
    printf("straphandler cause=[%d]%d sepc=%lx\n",csr::mcause::isInterrupt(scause),scause<<1>>1,sepc);
    kHartObjs.curtask->ctx.pc=(xlen_t)sepc;

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
        csrWrite(sepc,kHartObjs.curtask->ctx.pc);
    }
    // printf("mtraphandler over\n");
}
__attribute__((always_inline))
void _strapenter(){
    csrSwap(sscratch,t6);
    saveContext();
    extern xlen_t kstack_end;
    volatile register xlen_t sp asm("sp")=kstack_end;
    csrWrite(satp,kGlobObjs.ksatp);
    ExecInst(sfence.vma);
}
__attribute__((naked))
void _strapexit(){
    csrWrite(satp,kHartObjs.curtask->kctx.satp);
    ExecInst(sfence.vma);
    register ptr_t t6 asm("t6")=kHartObjs.curtask->ctx.gpr;
    restoreContext();
    csrSwap(sscratch,t6);
    ExecInst(sret);
}
extern "C" __attribute__((naked)) void strapwrapper(){
    _strapenter();
    straphandler();
    _strapexit();
}