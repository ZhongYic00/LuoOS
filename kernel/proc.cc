#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace proc;
// #define moduleLevel LogLevel::info
Process::Process(tid_t pid,prior_t prior,tid_t parent):IdManagable(pid),Scheduable(prior),parent(parent),vmar({}){
    kernel::createKernelMapping(vmar);
}
Process::Process(prior_t prior,tid_t parent):Process(id,prior,parent){}
xlen_t Process::newUstack(){
    auto ustack=UserStackDefault;
    using perm=vm::PageTableEntry::fieldMasks;
    vmar.map(vm::addr2pn(ustack),kGlobObjs->pageMgr->alloc(1),1,perm::r|perm::w|perm::u|perm::v);
    return ustack;
}
auto Process::newTrapframe(){
    // trapframe is mapped into proc space uniquely, sscratch is set accordingly
    xlen_t ppn=kGlobObjs->pageMgr->alloc(TrapframePages);
    auto vaddr=vm::pn2addr(ppn);
    using perm=vm::PageTableEntry::fieldMasks;
    /// @todo vaddr should be in specific region?
    vmar.map(vm::addr2pn(vaddr),ppn,1,perm::r|perm::w|perm::u|perm::v,vm::VMO::CloneType::alloc);
    return eastl::make_tuple(vaddr,vm::pn2addr(ppn));
}

Task* Process::newKTask(prior_t prior){
    auto [vaddr,buff]=newTrapframe();
    auto thrd=Task::createTask(**kGlobObjs->taskMgr,buff,prior,this->pid());
    thrd->lastpriv=Task::Priv::AlwaysKernel;
    addTask(thrd);
    kGlobObjs->scheduler->add(thrd);
    return thrd;
}
Task* Process::newTask(){
    auto [vaddr,buff]=newTrapframe();
    auto thrd=Task::createTask(**kGlobObjs->taskMgr,buff,0,this->pid());
    thrd->ctx.sp()=newUstack();
    thrd->ctx.a0()=0;
    thrd->kctx.vaddr=vaddr;
    addTask(thrd);
    kGlobObjs->scheduler->add(thrd);
    return thrd;
}

Task* Process::newTask(const Task &other,bool allocStack){
    auto [vaddr,buff]=newTrapframe();
    auto thrd=Task::createTask(**kGlobObjs->taskMgr,buff,other,this->pid());
    thrd->kctx.vaddr=vaddr;
    if(allocStack){
        thrd->ctx.sp()=newUstack();
    }
    addTask(thrd);
    kGlobObjs->scheduler->add(thrd);
    return thrd;
}

template<typename ...Ts>
Task* Task::createTask(ObjManager<Task> &mgr,xlen_t buff,Ts&& ...args){
    /**
     * @brief mem layout. low -> high
     * | task struct | kstack |
     */
    Task* task=reinterpret_cast<Task*>(buff);
    auto id=mgr.newId();
    new (task) Task(id,args...);
    mgr.addObj(id,task);
    return task;
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
    kHartObj().curtask=this;
    kctx.tp()=kernel::readHartId();
}
void Task::sleep(){
    Log(info,"sleep(this=Task<%d>)",this->id);
    kGlobObjs->scheduler->sleep(this);
}
void Process::print(){
    Log(info,"Process[%d]",pid());
    TRACE(vmar.print();)
    Log(info,"===========");
}
Process* proc::createKProcess(prior_t prior){
    auto proc=new (**kGlobObjs->procMgr) Process(0,0);
    proc->newKTask(prior);
    return proc;
}
Process* proc::createProcess(){
    // auto proc=kGlobObjs->progMgr->alloc(0,0);
    auto proc=new (**kGlobObjs->procMgr) Process(0,0);
    proc->newTask();
    static bool inited = false;
    if(inited) {
        if(proc->cwd == nullptr) { proc->cwd = fs::Path("/").pathSearch(); };
        proc->files[3]=eastl::make_shared<fs::File>(proc->cwd,0);
    }
    else { inited = true; }
    using op=fs::FileOp;
    proc->files[0]=eastl::make_shared<fs::File>(fs::File::stdin,op::read);
    proc->files[1]=eastl::make_shared<fs::File>(fs::File::stdout,op::write);
    proc->files[2]=eastl::make_shared<fs::File>(fs::File::stderr,op::write);
    DBG(proc->print();)
    Log(info,"proc created. pid=%d\n",proc->id);
    return proc;
}
Process* Task::getProcess(){ return (**kGlobObjs->procMgr)[proc]; }
proc::pid_t proc::clone(Task *task){
    auto proc=task->getProcess();
    Log(info,"clone(src=%p:[%d])",proc,proc->pid());
    TRACE(Log(info,"src proc VMAR:\n");proc->vmar.print();)
    auto newproc=new (**kGlobObjs->procMgr) Process(*proc);
    newproc->newTask(*task,false);
    newproc->defaultTask()->ctx.a0()=sys::statcode::ok;
    TRACE(newproc->vmar.print();)
    return newproc->pid();
}

Process::Process(const Process &other,tid_t pid):IdManagable(pid),Scheduable(other.prior),vmar(other.vmar),parent(other.id),cwd(other.cwd){
    for(int i=0;i<MaxOpenFile;i++)files[i]=other.files[i];
}
void Process::exit(int status){
    Log(info,"Proc[%d] exit(%d)",pid(),status);
    
    this->exitstatus=status;
    /// @todo resource recycle

    for(auto task:tasks){
        task->state=sched::Zombie;
        kGlobObjs->scheduler->remove(task);
    }

    state=sched::Zombie;

    auto task=parentProc()->defaultTask();
    kGlobObjs->scheduler->wakeup(task);
}
void Process::zombieExit(){
    Log(info,"Proc[%d] zombie exit",pid());
    kGlobObjs->procMgr->del(this);
}
Process::~Process(){
    for(auto task:tasks)kGlobObjs->taskMgr->del(task);
    tasks.clear();
}
Process *Process::parentProc(){return (**kGlobObjs->procMgr)[parent];}

int Process::fdAlloc(SharedPtr<File> a_file, int a_fd){ // fd缺省值为-1，在头文件中定义
    if(a_fd < 0) {
        for(int fd = 0; fd < MaxOpenFile; ++fd){
            if(files[fd] == nullptr){
                files[fd] = a_file;
                return fd;
            }
        }
    }
    else {
        if((a_fd<MaxOpenFile) && (files[a_fd]==nullptr)){
            files[a_fd] = a_file;
            return a_fd;
        }
    }
    return -1;  // 返回错误码
}