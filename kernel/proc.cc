#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace proc;

Process::Process(tid_t pid,prior_t prior,tid_t parent,vm::pgtbl_t pgtbl,Task *init):Scheduable(pid,prior),parent(parent),pagetable(pgtbl){
    tasks.push_back(init);
    kernel::createKernelMapping(pagetable);
    using perm=vm::PageTableEntry::fieldMasks;
    pagetable.createMapping(vm::addr2pn(UserStack),kernelPmgr->alloc(1),1,perm::r|perm::w|perm::u|perm::v);
    DBG(pagetable.print();)
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

void Task::switchTo(){
    // csrWrite(sscratch,task->ctx.gpr);
    // task->ctx.pc=0x80200000l;
    kLocObjs.ctx=this->ctx;
    csrWrite(sepc,this->ctx.pc);
    usatp=vm::PageTable::toSATP(this->getProcess()->pagetable);
    // task->getProcess()->pagetable.print();
    // csrWrite(satp,satp.value());
    // ExecInst(sfence.vma);
    // validate();
    // restoreContext();
    // ExecInst(sret);
}

int pidCnt;
Process *proclist[128];
Process* proc::createProcess(){
    auto pgtbl=reinterpret_cast<vm::pgtbl_t>(vm::pn2addr(kernelPmgr->alloc(1)));
    auto thrd=new Task(1,0,1,proc::UserStack);
    auto proc=new Process(++pidCnt,0,0,pgtbl,thrd);
    proclist[pidCnt]=proc;
    return proc;
}
Process* Task::getProcess(){ return proclist[proc]; }