
#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"

// #define moduleLevel LogLevel::debug

extern void schedule();
static hook_t hooks[]={schedule};

extern void nextTimeout();
void timerInterruptHandler(){
    for(auto hook:hooks)hook();
    nextTimeout();
}
extern syscall_t syscallPtrs[];
void uecallHandler(){
    /// @bug should get from ctx
    register int ecallId asm("a7");
    xlen_t &rtval=kHartObjs.curtask->ctx.x(10);
    kHartObjs.curtask->ctx.pc+=4;
    Log(debug,"uecall [%d]",ecallId);
    using namespace sys;
    if(ecallId<nSyscalls){
        csrSet(sstatus,BIT(csr::mstatus::sie));
        rtval=syscallPtrs[ecallId]();
        Log(info,"syscall %d %s",ecallId,rtval!=statcode::err?"success":"failed");
    } else {
        Log(warning,"syscall num exceeds valid range");
        rtval=1;
    }
    Log(debug,"uecall exit(id=%d,rtval=%d)",ecallId,rtval);
}

extern "C" void straphandler(){
    ptr_t sepc; csrRead(sepc,sepc);
    xlen_t scause; csrRead(scause,scause);
    xlen_t stval; csrRead(stval,stval);
    Log(debug,"straphandler cause=[%d]%d sepc=%lx stval=%lx\n",csr::mcause::isInterrupt(scause),scause<<1>>1,sepc,stval);
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
                Log(error,"scause=%d",scause);
                panic("unhandled interrupt!");
        }
    } else {
        switch(scause){
            using namespace csr::mcause;
            case uecall:uecallHandler();break;
            // case secall:break;
            case storeAccessFault:break;
            default:
                Log(error,"scause=%d",scause);
                panic("unhandled exception!");
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
    volatile register ptr_t sp asm("sp");
    // sp=(ptr_t)kstack_end;
    sp=kHartObjs.curtask->kctx.kstack;
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