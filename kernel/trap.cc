
#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"
#include "virtio.h"

// #define moduleLevel LogLevel::debug

extern void schedule();
static hook_t hooks[]={schedule};

extern void nextTimeout();
void timerInterruptHandler(){
    nextTimeout();
    for(auto hook:hooks)hook();
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
        /// @bug is this needed??
        // kHartObjs.curtask->lastpriv=proc::Task::Priv::Kernel;
        // csrSet(sscratch,kHartObjs.curtask->kctx.gpr);
        // csrSet(sstatus,BIT(csr::mstatus::sie));
        rtval=syscallPtrs[ecallId]();
        // csrClear(sstatus,BIT(csr::mstatus::sie));
        // csrSet(sscratch,kHartObjs.curtask->ctx.gpr);
        // kHartObjs.curtask->lastpriv=proc::Task::Priv::User;
        Log(info,"syscall %d %s",ecallId,rtval!=statcode::err?"success":"failed");
    } else {
        Log(warning,"syscall num exceeds valid range");
        rtval=1;
    }
    Log(debug,"uecall exit(id=%d,rtval=%d)",ecallId,rtval);
}
int plicClaim(){
    int hart=kernel::readHartId();
    int irq=mmio<int>(platform::plic::claimOf(hart));
    return irq;
}
void plicComplete(int irq){
    int hart=kernel::readHartId();
    mmio<int>(platform::plic::claimOf(hart))=irq;
}

void externalInterruptHandler(){
    int irq=plicClaim();
    Log(debug,"externalInterruptHandler(irq=%d)",irq);
    switch(irq){
        case 0:{
            Log(debug,"irq 0???");
            break;
        }
        case platform::uart0::irq:{
            using namespace platform::uart0;
            int cnt=0;
            while(mmio<volatile lsr>(reg(LSR)).rxnemp){
                char c=mmio<volatile uint8_t>(reg(RHR));
                cnt++;
            }
            if(cnt)Log(info,"uart: %d inputs",cnt);
            break;
        }
        case platform::virtio::blk::irq:{
            virtio_disk_intr();
            break;
        }
        default:
            panic("unknown irq!");
    }
    plicComplete(irq);
}


extern "C" void straphandler(){
    ptr_t sepc; csrRead(sepc,sepc);
    xlen_t scause; csrRead(scause,scause);
    xlen_t stval; csrRead(stval,stval);
    Log(debug,"straphandler cause=[%d]%d sepc=%lx stval=%lx\n",csr::mcause::isInterrupt(scause),scause<<1>>1,sepc,stval);
    if(kHartObjs.curtask->lastpriv==proc::Task::Priv::User)kHartObjs.curtask->ctx.pc=(xlen_t)sepc;
    else kHartObjs.curtask->kctx.pc=(xlen_t)sepc;

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
            case sei: externalInterruptHandler(); break;
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
    if(kHartObjs.curtask->lastpriv!=proc::Task::Priv::User)kHartObjs.curtask->switchTo();
}
__attribute__((always_inline))
void _strapenter(){
    csrSwap(sscratch,t6);
    saveContext();
    extern xlen_t kstack_end;
    csrWrite(satp,kGlobObjs.ksatp);
    ExecInst(sfence.vma);
    volatile register ptr_t sp asm("sp");
    // sp=(ptr_t)kstack_end;
    sp=kHartObjs.trapstack;
    volatile register xlen_t tp asm("tp");
    tp=kHartObjs.curtask->kctx.tp();
}
__attribute__((always_inline))
void _strapexit(){
    auto cur=kHartObjs.curtask;
    /// @todo chaos
    if(cur->lastpriv==proc::Task::Priv::User){
        kHartObjs.trapstack=cur->kctx.kstack;
        csrWrite(sscratch,cur->ctx.gpr);
        csrWrite(sepc,cur->ctx.pc);
        csrClear(sstatus,1l<<csr::mstatus::spp);
        csrSet(sstatus,BIT(csr::mstatus::spie));
        csrWrite(satp,kHartObjs.curtask->kctx.satp);
        ExecInst(sfence.vma);
        register ptr_t t6 asm("t6")=kHartObjs.curtask->ctx.gpr;
        restoreContext();
        csrSwap(sscratch,t6);
        ExecInst(sret);
    } else {
        if(cur->lastpriv!=proc::Task::Priv::AlwaysKernel)cur->lastpriv=proc::Task::Priv::User;
        // csrWrite(satp,kctx.satp);
        // ExecInst(sfence.vma);
        kHartObjs.trapstack=(ptr_t)kInfo.segments.kstack.first+0x1000;
        csrWrite(sscratch,cur->kctx.gpr);
        volatile register ptr_t t6 asm("t6")=cur->kctx.gpr;
        restoreContext();
        /// @bug suppose this swap has problem when switching process
        csrSwap(sscratch,t6);
        csrSet(sstatus,BIT(csr::mstatus::sie));
        ExecInst(ret);
    }
}
extern "C" __attribute__((naked)) void strapwrapper(){
    _strapenter();
    straphandler();
    _strapexit();
}