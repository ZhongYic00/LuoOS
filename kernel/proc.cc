#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace proc;

Process::Process(tid_t pid,prior_t prior,tid_t parent,vm::pgtbl_t pgtbl):Scheduable(pid,prior),parent(parent),pagetable(pgtbl){
    kernel::createKernelMapping(pagetable);
    DBG(pagetable.print();)
}
xlen_t Process::newUstack(){
    auto ustack=UserStack;
    using perm=vm::PageTableEntry::fieldMasks;
    pagetable.createMapping(vm::addr2pn(ustack),kernelPmgr->alloc(1),1,perm::r|perm::w|perm::u|perm::v);
    return ustack;
}
xlen_t Process::newKstack(){
    auto kstack=kInfo.segments.kstack.second;
    using perm=vm::PageTableEntry::fieldMasks;
    pagetable.createMapping(vm::addr2pn(kstack),kernelPmgr->alloc(1),1,perm::r|perm::w|perm::u|perm::v);
    return kstack;
}

Task* Process::newTask(){
    auto thrd=kGlobObjs.taskMgr.alloc(0,this->pid(),proc::UserStack);
    thrd->ctx.sp()=newUstack();
    thrd->kctx.kstack=(ptr_t)newKstack();
    addTask(thrd);
}

void validate(){
    xlen_t *sepc;csrRead(sepc,sepc);
    csrSet(sstatus,BIT(csr::mstatus::sum));
    bool b;
    for(int i=0;i<8;i++){
        b^=sepc[1<<i]<sepc[1<<i|1];
    }
    // csrClear(sstatus,BIT(csr::mstatus::sum));
}

extern void _strapexit();
void Task::switchTo(){
    // task->ctx.pc=0x80200000l;
    kHartObjs.curtask=this;
    if(lastpriv==Priv::User){
        csrWrite(sscratch,ctx.gpr);
        csrWrite(sepc,ctx.pc);
        auto proc=getProcess();
    } else {
        lastpriv=Priv::User;
        csrWrite(satp,kctx.satp);
        ExecInst(sfence.vma);
        register ptr_t t6 asm("t6")=kctx.gpr;
        restoreContext();
        ExecInst(ret);
    }
    // task->getProcess()->pagetable.print();
}
void Task::sleep(){
    kGlobObjs.scheduler.sleep(this);
    lastpriv=Priv::Kernel;
    // register xlen_t sp asm("sp");
    // saveContextTo(kctx.gpr);
}

Task* TaskManager::alloc(prior_t prior,tid_t prnt,xlen_t stack){
    ++tidCnt;
    return tasklist[tidCnt]=new Task(tidCnt,prior,prnt,stack);
}
Process *ProcManager::alloc(prior_t prior,tid_t prnt,vm::pgtbl_t pgtbl){
    ++pidCnt;
    return proclist[pidCnt]=new Process(pidCnt,prior,prnt,pgtbl);
}
Process* proc::createProcess(){
    auto pgtbl=reinterpret_cast<vm::pgtbl_t>(vm::pn2addr(kernelPmgr->alloc(1)));
    auto proc=kGlobObjs.procMgr.alloc(0,0,pgtbl);
    proc->newTask();
    proc->files[0]=new fs::File;
    proc->files[0]->type=fs::File::pipe;
    printf("proc created. pid=%d\n",proc->id);
    return proc;
}
Process* Task::getProcess(){ return kGlobObjs.procMgr[proc]; }