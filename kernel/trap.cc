
#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"
#include "virtio.h"

#define moduleLevel LogLevel::trace

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
    int ecallId=kHartObjs.curtask->ctx.x(17);
    xlen_t &rtval=kHartObjs.curtask->ctx.x(10);
    kHartObjs.curtask->ctx.pc+=4;
    Log(debug,"uecall [%d]",ecallId);
    using namespace sys;
    kHartObjs.curtask->lastpriv=proc::Task::Priv::Kernel;
    if(ecallId<nSyscalls){
        /// @bug is this needed??
        // kHartObjs.curtask->lastpriv=proc::Task::Priv::Kernel;
        // csrSet(sscratch,kHartObjs.curtask->kctx.gpr);
        // csrSet(sstatus,BIT(csr::mstatus::sie));
        rtval=syscallPtrs[ecallId]();
        csrClear(sstatus,BIT(csr::mstatus::sie));
        Log(debug,"syscall %d %s",ecallId,rtval!=statcode::err?"success":"failed");
    } else {
        Log(warning,"syscall num exceeds valid range");
        rtval=1;
    }
    kHartObjs.curtask->lastpriv=proc::Task::Priv::User;
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
    Log(trace,"strap enter, saved context=%s",kHartObjs.curtask->toString(true).c_str());
    if(kHartObjs.curtask->lastpriv==proc::Task::Priv::User)kHartObjs.curtask->ctx.pc=(xlen_t)sepc;
    else kHartObjs.curtask->kctx.ra()=(xlen_t)sepc;

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
                Log(error,"interrupt[%d]",scause);
                panic("unhandled interrupt!");
        }
    } else {
        switch(scause){
            using namespace csr::mcause;
            case uecall:uecallHandler();break;
            // case secall:break;
            case storeAccessFault:break;
            default:
                Log(error,"exception[%d] sepc=%x stval=%x",scause,sepc,stval);
                panic("unhandled exception!");
        }
        csrWrite(sepc,kHartObjs.curtask->ctx.pc);
    }
    // printf("mtraphandler over\n");
    // if(kHartObjs.curtask->lastpriv!=proc::Task::Priv::User)kHartObjs.curtask->switchTo();

    Log(debug,"strap exit, restore context=%s",kHartObjs.curtask->toString().c_str());
}
__attribute__((always_inline))
void _strapenter(){
    csrSwap(sscratch,t6);
    saveContext();
    extern xlen_t kstack_end;
    csrRead(satp,kGlobObjs.prevsatp);
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
        xlen_t gprvaddr=kInfo.segments.kstack.second+(xlen_t)(cur->ctx.gpr)-(xlen_t)cur;
        csrWrite(sscratch,gprvaddr);
        csrWrite(sepc,cur->ctx.pc);
        csrClear(sstatus,1l<<csr::mstatus::spp);
        csrSet(sstatus,BIT(csr::mstatus::spie));
        csrWrite(satp,kHartObjs.curtask->kctx.satp);
        ExecInst(sfence.vma);
        register xlen_t t6 asm("t6");
        csrRead(sscratch,t6);
        restoreContext();
        csrSwap(sscratch,t6);
        ExecInst(sret);
    } else {
        // csrWrite(satp,kctx.satp);
        // ExecInst(sfence.vma);
        kHartObjs.trapstack=(ptr_t)kInfo.segments.kstack.first+0x1000;
        // xlen_t gprvaddr=kInfo.segments.kstack.second+((xlen_t)cur->kctx.gpr-(xlen_t)cur);
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