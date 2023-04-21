#include "sched.hh"
#include "rvcsr.hh"

using sched::Process,sched::Task;
Task *tmp;
void validate(){
    xlen_t *sepc;csrRead(sepc,sepc);
    csrSet(sstatus,BIT(csr::mstatus::sum));
    bool b;
    for(int i=0;i<8;i++){
        b^=sepc[1<<i]<sepc[1<<i|1];
    }
    // csrClear(sstatus,BIT(csr::mstatus::sum));
}
void switchTo(Task *task){
    // csrWrite(sscratch,task->ctx.gpr);
    // task->ctx.pc=0x80200000l;
    csrWrite(sepc,task->ctx.pc);
    ctx=task->ctx;
    csr::satp satp;
    satp.mode=8;
    satp.asid=0;
    satp.ppn=vm::addr2pn((xlen_t)task->getProcess()->pagetable.getRoot());
    usatp=satp.value();
    // task->getProcess()->pagetable.print();
    // csrWrite(satp,satp.value());
    // ExecInst(sfence.vma);
    // validate();
    // restoreContext();
    // ExecInst(sret);
}
void schedule(){
    printf("scheduling\n");
    switchTo(tmp);
}
int pidCnt;
Process *proclist[128];
Process* sched::createProcess(){
    auto pgtbl=reinterpret_cast<vm::pgtbl_t>(vm::pn2addr(kernelPmgr->alloc(1)));
    auto thrd=new Task(1,0,1,sched::UserStack);
    tmp=thrd;
    auto proc=new Process(++pidCnt,0,0,pgtbl,thrd);
    proclist[pidCnt]=proc;
    return proc;
}
Process* Task::getProcess(){ return proclist[proc]; }