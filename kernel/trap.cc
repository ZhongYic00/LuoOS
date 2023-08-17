
#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"
#include "virtio.hh"
#include "ipc.hh"
#include "time.hh"
#include <EASTL/chrono.h>

#define moduleLevel LogLevel::warning

static hook_t hooks[]={schedule};

// extern void nextTimeout();
void ktrapwrapper();
void strapwrapper();
void timerInterruptHandler(){
    xlen_t time;
    csrRead(time,time);
    Log(info,"timerInterrupt @ %ld",time);
    auto cur = kHartObj().curtask;
    // ++kHartObj().g_ticks;
    // for(int i = 0; i < kernel::NMAXSLEEP; ++i) {
    //     auto towake = kHartObj().sleep_tasks[i];
    //     if(towake.m_task!=nullptr && towake.m_wakeup_tick<kHartObj().g_ticks) {
    //         kGlobObjs->scheduler->wakeup(towake.m_task);
    //         kHartObj().sleep_tasks[i].m_task = nullptr;
    //     }
    // }
    xlen_t sstatus;
    csrRead(sstatus,sstatus);
    if(cur->lastpriv!=proc::Task::Priv::AlwaysKernel&&sstatus&BIT(csr::mstatus::spp))panic("should not happen!");
    kHartObj().timer->next();
}
extern syscall_t syscallPtrs[];
namespace syscall{ extern const char* syscallHelper[]; }
void uecallHandler(){
    auto cur=kHartObj().curtask;
    auto &ctx=cur->ctx;
    int ecallId=ctx.x(17);
    xlen_t &rtval=ctx.x(10);
    ctx.pc+=4;
    Log(trace,"uecall [%d]",ecallId);
    using namespace sys;
    cur->lastpriv=proc::Task::Priv::Kernel;
    if(ecallId<nSyscalls){
        Log(syscallPtrs[ecallId]?info:error,"proc called %s[%d]",syscall::syscallHelper[ecallId],ecallId);
        /// @bug is this needed??
        // cur->lastpriv=proc::Task::Priv::Kernel;
        csrWrite(sscratch,cur->kctx.gpr);
        // csrSet(sstatus,BIT(csr::mstatus::sie));
        if(syscallPtrs[ecallId])
            rtval=reinterpret_cast<syscall_t>(syscallPtrs[ecallId])(ctx.x(10),ctx.x(11),ctx.x(12),ctx.x(13),ctx.x(14),ctx.x(15));
        // csrClear(sstatus,BIT(csr::mstatus::sie));
        Log(debug,"syscall %d %s",ecallId,rtval!=statcode::err?"success":"failed");
    } else {
        Log(error,"syscall num{%d} exceeds valid range",ecallId);
        rtval=1;
    }
    cur->lastpriv=proc::Task::Priv::User;
    Log(debug,"uecall exit(id=%d,rtval=%d)",ecallId,rtval);
}
int plicClaim(){
    int hart=readHartId();
    int irq=mmio<int>(platform::plic::claimOf(hart));
    return irq;
}
void plicComplete(int irq){
    int hart=readHartId();
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
bool isPrevPriv(){xlen_t sstatus;csrRead(sstatus,sstatus);return sstatus&BIT(csr::mstatus::spp);}
void pagefaultHandler(xlen_t addr){
    if(isPrevPriv()){
        xlen_t satp;
        csrRead(satp,satp);
        assert(satp==kGlobObjs->vmar->satp());
        kGlobObjs->vmar->pfhandler(addr);
    } else {
        kHartObj().curtask->getProcess()->vmar.pfhandler(addr);
    }
}

extern "C" void straphandler(){
    ptr_t sepc; csrRead(sepc,sepc);
    xlen_t scause; csrRead(scause,scause);
    xlen_t stval; csrRead(stval,stval);
    Log(debug,"straphandler cause=[%d]%d sepc=%lx stval=%lx\n",csr::mcause::isInterrupt(scause),scause<<1>>1,sepc,stval);
    Log(debug,"strap enter, saved context=%s",kHartObj().curtask->toString().c_str());

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
            case instrPageFault:
            case loadPageFault:
            case storePageFault:
                pagefaultHandler(stval);
                break;
            default:
                Log(error,"exception[%d]{%s} sepc=%x stval=%x",scause,csr::mcause::exceptionsHelper[scause],sepc,stval);
                // kHartObj().curtask->kctx.ra()=(xlen_t)sepc+4;
                // break;
                panic("unhandled exception!");
        }
    }
    if(kHartObj().curtask->lastpriv==proc::Task::Priv::User){
        kHartObj().curtask->lastpriv=proc::Task::Priv::Kernel;
        signal::sigHandler();
        kHartObj().curtask->lastpriv=proc::Task::Priv::User;

    }
    // printf("mtraphandler over\n");
    // if(kHartObj().curtask->lastpriv!=proc::Task::Priv::User)kHartObj().curtask->switchTo();

    Log(debug,"strap exit, restore context=%s",kHartObj().curtask->toString().c_str());
}
template<bool fromUmode=true>
__attribute__((always_inline))
void _strapenter(){
    csrSwap(sscratch,t6);
    saveContext();
    // after saveContext t6=sscratch
    ptr_t t6;
    regRead(t6,t6);
    if constexpr(fromUmode){
        auto curtask=proc::Task::gprToTask(t6);
        csrWrite(stvec,ktrapwrapper);
        csrWrite(sscratch,curtask->kctx.gpr);
        regWrite(tp,curtask->kctx.tp());
        csrWrite(satp,kGlobObjs->ksatp);
        ExecInst(sfence.vma);
        regWrite(sp,kHartObj().curtask->trapstack());
        csrRead(sepc,kHartObj().curtask->ctx.pc);
        // assert(curtask->trapstack()&0xff==0);
    } else {
        // __asm__("addi sp,sp,-16\nsd ra,0(sp)\ncsrr ra,sepc\nsd ra,8(sp)");
        // csrSwap(sscratch,t6);
        // __asm__("addi t6,sp,-248");
        // saveContext();
        // __asm__("addi sp,sp,-248");
        /// @todo set stack
        // int tp;
        // regRead(tp,tp);
        // regWrite(sp,kInfo.segments.kstack.first+tp*0x1000+0x1000);
        auto curtask=kHartObj().curtask;
        csrRead(sepc,curtask->kctx.pc);
        curtask->kctxs.push(curtask->kctx);
        csrWrite(sscratch,curtask->kctx.gpr);
    }
}
__attribute__((always_inline))
void _strapexit(){
    auto cur=kHartObj().curtask;
    static xlen_t prevs0=0;
    /// @todo chaos
    if(cur->lastpriv==proc::Task::Priv::User){
        // xlen_t gprvaddr=kInfo.segments.kstack.first+readHartId()*0x1000+offsetof(proc::Task,ctx.gpr);
        auto gprvaddr=cur->kctx.vaddr+offsetof(proc::Task,ctx.gpr);
        csrWrite(sscratch,gprvaddr);
        csrWrite(sepc,cur->ctx.pc);
        assert(!(cur->ctx.pc>=0x80200000&&cur->ctx.pc<=0x80300000));
        csrClear(sstatus,1l<<csr::mstatus::spp);
        csrSet(sstatus,BIT(csr::mstatus::spie));
        csrWrite(stvec,strapwrapper);
        if(cur->ctx.pc<0x100000)
            prevs0--;
        csrWrite(satp,kHartObj().curtask->kctx.satp);
        ExecInst(sfence.vma);
        asm("csrr t6,sscratch");
        restoreContext();
        ExecInst(sret);
    } else {
        csrSet(sstatus,1l<<csr::mstatus::spp);
        csrWrite(stvec,ktrapwrapper);
        csrWrite(sscratch,cur->kctx.gpr);
        if(cur->lastpriv==proc::Task::Priv::AlwaysKernel)csrSet(sstatus,BIT(csr::mstatus::spie))
        else csrClear(sstatus,BIT(csr::mstatus::spie))
        assert(!cur->kctxs.empty());
        cur->kctx=cur->kctxs.top();cur->kctxs.pop();
        csrWrite(sepc,cur->kctx.pc);
        regWrite(t6,cur->kctx.gpr);
        // __asm__("mv t6,sp");
        restoreContext();
        // __asm__("ld ra,8(sp)\ncsrw sepc,ra\nld ra,0(sp)\n addi sp,sp,16");
        ExecInst(sret);
    }
}
__attribute__((naked)) void strapwrapper(){
    _strapenter();
    straphandler();
    _strapexit();
}
__attribute__((naked))
void ktrapwrapper(){
    _strapenter<false>();
    straphandler();
    _strapexit();
}