#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace proc;

#define DEBUG 1
Process::Process(tid_t pid,prior_t prior,tid_t parent):IdManagable(pid),Scheduable(prior),parent(parent),vmar({}){
    kernel::createKernelMapping(vmar);
}
Process::Process(prior_t prior,tid_t parent):Process(id,prior,parent){}
xlen_t Process::newUstack(){
    auto ustack=UserStackDefault;
    using perm=vm::PageTableEntry::fieldMasks;
    vmar.map(vm::addr2pn(ustack),kGlobObjs.pageMgr->alloc(1),1,perm::r|perm::w|perm::u|perm::v);
    return ustack;
}
xlen_t Process::newKstack(){
    auto kstackv=kInfo.segments.kstack.second;
    xlen_t kstackppn;
    using perm=vm::PageTableEntry::fieldMasks;
    vmar.map(vm::addr2pn(kstackv),kstackppn=kGlobObjs.pageMgr->alloc(1),1,perm::r|perm::w|perm::u|perm::v,vm::VMO::CloneType::alloc);
    return vm::pn2addr(kstackppn+1)-1;
}

Task* Process::newTask(){
    // auto thrd=kGlobObjs.taskMgr.alloc(0,this->pid(),proc::UserStackDefault);
    auto thrd=new (kGlobObjs.taskMgr) Task(0,this->pid(),proc::UserStackDefault);
    thrd->ctx.sp()=newUstack();
    thrd->kctx.kstack=(ptr_t)newKstack();
    addTask(thrd);
    kGlobObjs.scheduler.add(thrd);
}

Task* Process::newTask(const Task &other,bool allocStack){
    auto thrd=new (kGlobObjs.taskMgr) Task(other,this->pid());
    if(allocStack){
        thrd->ctx.sp()=newUstack();
        thrd->kctx.kstack=(ptr_t)newKstack();
    }
    addTask(thrd);
    kGlobObjs.scheduler.add(thrd);
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
        // csrWrite(satp,kctx.satp);
        // ExecInst(sfence.vma);
        register ptr_t t6 asm("t6")=kctx.gpr;
        restoreContext();
        ExecInst(ret);
    }
    // task->getProcess()->vmar.print();
}
void Task::sleep(){
    kGlobObjs.scheduler.sleep(this);
    // register xlen_t sp asm("sp");
    // saveContextTo(kctx.gpr);
}
void Process::print(){
    printf("Process[%d]\n",pid());
    vmar.print();
    printf("===========");
}
Process* proc::createProcess(){
    // auto proc=kGlobObjs.procMgr.alloc(0,0);
    auto proc=new (kGlobObjs.procMgr) Process(0,0);
    proc->newTask();
    proc->files[0]=new fs::File;
    proc->files[0]->type=fs::File::stdout;
    DBG(proc->print();)
    printf("proc created. pid=%d\n",proc->id);
    return proc;
}
Process* Task::getProcess(){ return kGlobObjs.procMgr[proc]; }
void proc::clone(Task *task){
    auto proc=task->getProcess();
    DBG(printf("src proc VMAR:\n");proc->vmar.print();)
    auto newproc=new (kGlobObjs.procMgr) Process(*proc);
    newproc->newTask(*task,false);
    newproc->defaultTask()->ctx.a0()=1;
    DBG(newproc->vmar.print();)
}

Process::Process(const Process &other,tid_t pid):IdManagable(pid),Scheduable(other.prior),vmar(other.vmar){
    
}