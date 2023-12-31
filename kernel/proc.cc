#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"
#include "vm/vmo.hh"
#include <linux/futex.h>

using namespace proc;
// #define moduleLevel LogLevel::info
Process::Process(pid_t pid,prior_t prior,pid_t parent):IdManagable(pid),Scheduable(prior),parent(parent),vmar({}){
    kernel::createKernelMapping(vmar);
    using namespace vm;
    using perm=PageTableEntry::fieldMasks;
    using mapping=PageMapping::MappingType;
    using sharing=PageMapping::SharingType;
    vmar.map(PageMapping{addr2pn(vDSOBase),vDSOPages,0,kInfo.vmos.vdso,perm::r|perm::x|perm::u|perm::v,mapping::system,sharing::shared});
}
Process::Process(prior_t prior,pid_t parent):Process(id,prior,parent){}
xlen_t Process::newUstack(){
    auto ustack=UserStackDefault,ustackpages=vm::bytes2pages(UstackSize);
    using perm=vm::PageTableEntry::fieldMasks;
    using namespace vm;
    auto vmo=make_shared<VMOContiguous>(kGlobObjs->pageMgr->alloc(ustackpages),ustackpages);
    // auto pager=make_shared<SwapPager>(nullptr,Segment{0,0});
    // auto vmo=make_shared<VMOPaged>(ustackpages,pager);
    vmar.map(PageMapping{vm::addr2pn(UstackBottom),ustackpages,0,vmo,perm::r|perm::w|perm::u|perm::v,PageMapping::MappingType::system,PageMapping::SharingType::privt});
    return ustack;
}
auto Process::newTrapframe(){
    // trapframe is mapped into proc space uniquely, sscratch is set accordingly
    xlen_t ppn=kGlobObjs->pageMgr->alloc(TrapframePages);
    auto vaddr=vm::pn2addr(ppn);
    using perm=vm::PageTableEntry::fieldMasks;
    using namespace vm;
    /// @todo vaddr should be in specific region?
    auto vmo=make_shared<VMOContiguous>(ppn,TrapframePages);
    vmar.map(PageMapping{vm::addr2pn(vaddr),TrapframePages,0,vmo,perm::r|perm::w|perm::u|perm::v,PageMapping::MappingType::system,PageMapping::SharingType::privt});
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

Task* Process::newTask(const Task &other,addr_t ustack){
    auto [vaddr,buff]=newTrapframe();
    auto thrd=Task::createTask(**kGlobObjs->taskMgr,buff,other,this->pid());
    thrd->kctx.vaddr=vaddr;
    if(!ustack){
        thrd->ctx.sp()=newUstack();
    } else {
        thrd->ctx.sp()=ustack;
    }
    addTask(thrd);
    kGlobObjs->scheduler->add(thrd);
    return thrd;
}
xlen_t Process::brk(xlen_t addr){
    Log(info,"brk %lx",addr);
    if(addr>=heapBottom+UserHeapSize||addr<=heapBottom)return heapTop;
    if(vmar.contains(addr))return heapTop=addr;
    else {
        /// @todo free redundant vmar
        /// @brief alloc new vmar
        using namespace vm;
        xlen_t curtop=bytes2pages(heapTop),
            destop=bytes2pages(addr),
            pages=destop-curtop;
        Log(info,"curtop=%x,destop=%x, needs to alloc %d pages",curtop,destop,pages);
        auto pager=make_shared<SwapPager>(nullptr,Segment{0x0,pn2addr(pages)});
        auto vmo=make_shared<VMOPaged>(pages,pager);
        using perm=PageTableEntry::fieldMasks;
        vmar.map(PageMapping{curtop,pages,0,vmo,perm::r|perm::w|perm::x|perm::u|perm::v,PageMapping::MappingType::anon,PageMapping::SharingType::privt});
        return heapTop=addr;
    }
}
ByteArray Process::getGroups(int a_size) {
    ArrayBuff<gid_t> grps(a_size);
    int i = 0;
    for(auto gid: supgids) {
        if(i >= a_size) { break; }
        grps.buff[i] = gid;
        ++i;
    }
    return ByteArray((uint8*)grps.buff, i * sizeof(gid_t));
}
void Process::setGroups(ArrayBuff<gid_t> a_grps) {
    clearGroups();
    for(auto gid: a_grps) { supgids.emplace(gid); }
    return;
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

void Task::switchTo(){
    kHartObj().curtask=this;
    kctx.tp()=readHartId();
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
        proc->files[FdCwd] = make_shared<File>(proc->cwd,0);
    }
    else { inited = true; }
    using FileOp = fs::FileOp;
    using FileType = fs::FileType;
    proc->files[0] = make_shared<File>(FileType::stdin, FileOp::read);
    proc->files[1] = make_shared<File>(FileType::stdout, FileOp::write);
    proc->files[2] = make_shared<File>(FileType::stderr, FileOp::write);
    DBG(proc->print();)
    Log(info,"proc created. pid=%d\n",proc->id);
    return proc;
}
Process* Task::getProcess(){ return (**kGlobObjs->procMgr)[proc]; }
pid_t proc::clone(Task *task){
    auto proc=task->getProcess();
    Log(info,"clone(src=%p:[%d])",proc,proc->pid());
    TRACE(Log(info,"src proc VMAR:\n");proc->vmar.print();)
    auto newproc=new (**kGlobObjs->procMgr) Process(*proc);
    newproc->newTask(*task,task->ctx.sp());
    newproc->defaultTask()->ctx.a0()=sys::statcode::ok;
    TRACE(newproc->vmar.print();)
    return newproc->pid();
}

Process::Process(const Process &other,pid_t pid):IdManagable(pid),Scheduable(other.prior),vmar(other.vmar),parent(other.id),cwd(other.cwd),
    heapTop(other.heapBottom),heapBottom(other.heapBottom){
    for(int i=0;i<mOFiles;i++)files[i]=other.files[i];
}
void Process::exit(int status){
    Log(info,"Proc[%d] exit",pid());
    state=sched::Zombie;
    exitstatus=status;
    // signal::sigSend(*parentProc(),SIGCHLD);
    kGlobObjs->scheduler->wakeup(parentProc()->defaultTask());
    kernel::yield();
}
void Process::zombieExit(){
    Log(info,"Proc[%d] zombie exit",pid());
    for(auto child:kGlobObjs->procMgr->getChilds(pid())){
        child->parent=parent;
    }
    kGlobObjs->procMgr->del(pid());
}
Process::~Process(){
}
Process *Process::parentProc(){return (**kGlobObjs->procMgr)[parent];}
shared_ptr<File> Process::ofile(int a_fd) {
    if(a_fd == AT_FDCWD) { a_fd = FdCwd; }
    if(fdOutRange(a_fd)) { return nullptr; }
    return files[a_fd];
}
int Process::fdAlloc(shared_ptr<File> a_file, int a_fd, bool a_appoint) {
    if(!a_appoint) {  // 在不小于a_fd的文件描述符中分配一个
        for(int fd = (a_fd<0 ? 0 : a_fd); fd < mOFiles; ++fd) {
            if(files[fd] == nullptr) {
                files[fd] = a_file;
                return fd;
            }
        }
        return -ENOMEM;
    }
    else {  // 明确要求在a_fd处分配
        if(fdOutRange(a_fd)) { return -EBADF; }
        files[a_fd] = a_file;
        return a_fd;
    }
}
int Process::setUMask(mode_t a_mask) {
    mode_t ret = umask;
    umask = a_mask & 0777 ;
    return ret;
}
int Process::setRLimit(int a_rsrc, const RLim *a_rlim) {
    if(a_rlim == nullptr) { return -1; }
    if (m_euid!=0 && (a_rlim->rlim_cur>rlimits[a_rsrc].rlim_max || a_rlim->rlim_max>rlimits[a_rsrc].rlim_max)) { return -1; }
    rlimits[a_rsrc] = *a_rlim;
    return 0;
}

Task::~Task(){}
namespace syscall{
    int futex(int *uaddr, int futex_op, int val,
                 eastl::optional<eastl::chrono::nanoseconds> timeout,   /* or: uint32_t val2 */
                 int *uaddr2, int val3);
}
void Task::exit(int status){
    Log(info,"Task[%d] exit(%d)",tid(),status);
    attrs.exitstatus=status;
    state=sched::Zombie;
    kGlobObjs->scheduler->remove(this);
    auto curproc=getProcess();
    curproc->tasks.erase(this);
    if(attrs.clearChildTid){
        curproc->vmar[(addr_t)attrs.clearChildTid]<<0;
        syscall::futex(attrs.clearChildTid,FUTEX_WAKE,1,{},nullptr,0);
    }
    if(curproc->tasks.empty())
        curproc->exit(status);
    else
        signal::sigSend(*curproc,SIGCHLD);
    kernel::yield();
}