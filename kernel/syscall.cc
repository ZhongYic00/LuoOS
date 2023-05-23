#include "kernel.hh"
#include "sched.hh"

syscall_t syscallPtrs[sys::syscalls::nSyscalls];
extern void _strapexit();
namespace syscall
{
    using sys::statcode;
    using klib::SharedPtr;
    // using klib::make_shared;
    int none(){return 0;}
    int testexit(){
        static bool b=false;
        b=!b;
        if(b)return 1;
        return -1;
    }
    int read(){
        auto &ctx=kHartObjs.curtask->ctx;
        int fd=ctx.x(10);
        xlen_t uva=ctx.x(11),len=ctx.x(12);
        auto file=kHartObjs.curtask->getProcess()->ofile(fd);
        return file->read(uva,len);
    }
    int write(){
        auto &ctx=kHartObjs.curtask->ctx;
        xlen_t fd=ctx.x(10),uva=ctx.x(11),len=ctx.x(12);
        auto file=kHartObjs.curtask->getProcess()->ofile(fd);
        return file->write(uva,len);
    }
    __attribute__((naked))
    void sleepSave(ptr_t gpr){
        saveContextTo(gpr);
        schedule();
        _strapexit(); //TODO check
    }
    void yield(){
        Log(info,"yield!");
        auto &cur=kHartObjs.curtask;
        cur->lastpriv=proc::Task::Priv::Kernel;
        sleepSave(cur->kctx.gpr);
    }
    int sysyield(){
        yield();
        return statcode::ok;
    }
    int getPid(){
        return kHartObjs.curtask->getProcess()->pid();
    }
    int sleep(){
        auto &cur=kHartObjs.curtask;
        cur->sleep();
        yield();
        return statcode::ok;
    }
    int clone(){
        auto &ctx=kHartObjs.curtask->ctx;
        auto pid=proc::clone(kHartObjs.curtask);
        return pid;
    }
    int openAt() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_dirfd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        int a_flags = ctx.x(12);
        mode_t a_mode = ctx.x(13); // uint32

        int fd;
        SharedPtr<fs::File> f;
        auto curproc = kHartObjs.curtask->getProcess();

        /*
            inode相关操作
        */
        // 测试用
        SharedPtr<fs::INode> in = new fs::INode;
        in->m_type = fs::INode::file;
        //

        
        if(in->m_type == fs::INode::dev) {
            f = new fs::File(fs::File::FileType::dev, in);
        }
        else {
            f = new fs::File(fs::File::FileType::inode, in);
        }
        fd = curproc->fdAlloc(f);
        if(fd < 0) { return statcode::err; }

        return fd;
    }
    int close() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);

        SharedPtr<fs::File> f;
        auto curproc = kHartObjs.curtask->getProcess();
        // 判断fd范围也许可以写成宏……
        if((a_fd<0) || (a_fd>proc::MaxOpenFile)) { return statcode::err; }
        
        f = curproc->files[a_fd];
        if(f == nullptr) { return statcode::err; }

        curproc->files[a_fd].deRef();
        return statcode::ok;
    }
    int dupArgsIn(int a_fd, int a_newfd=-1) {
        int newfd;
        SharedPtr<fs::File> f;
        auto curproc = kHartObjs.curtask->getProcess();
        // 判断fd范围也许可以写成宏……
        if((a_fd<0) || (a_fd>proc::MaxOpenFile)) { return statcode::err; }
        
        f = curproc->files[a_fd];
        if(f == nullptr) { return statcode::err; }
        // dupArgsIn内部newfd<0时视作由操作系统分配描述符（同fdAlloc），因此对newfd非负的判断应在外层dup3中完成
        newfd = curproc->fdAlloc(f, a_newfd);
        if(newfd < 0) { return statcode::err; }

        return newfd;
    }
    int dup() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);

        return dupArgsIn(a_fd);
    }
    int dup3() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);
        int a_newfd = ctx.x(11);
        // 判断fd范围也许可以写成宏……
        if((a_newfd<0) || (a_newfd>proc::MaxOpenFile)){ return statcode::err; }

        return dupArgsIn(a_fd, a_newfd);
    }
    int pipe2(){
        auto &cur=kHartObjs.curtask;
        auto &ctx=cur->ctx;
        auto proc=cur->getProcess();
        xlen_t fd=ctx.x(10),flags=ctx.x(11);
        auto pipe=SharedPtr<pipe::Pipe>(new pipe::Pipe);
        auto rfile=SharedPtr<fs::File>(new fs::File(pipe,fs::File::FileOp::read));
        auto wfile=SharedPtr<fs::File>(new fs::File(pipe,fs::File::FileOp::write));
        int fds[]={proc->fdAlloc(rfile),proc->fdAlloc(wfile)};
        proc->vmar[fd]=fds;
    }
    int exit(){
        auto status=kHartObjs.curtask->ctx.a0();
        kHartObjs.curtask->getProcess()->exit(status);
        yield();
    }
    int waitpid(tid_t pid,xlen_t wstatus,int options){
        Log(info,"waitpid(pid=%d,options=%d)",pid,options);
        auto curproc=kHartObjs.curtask->getProcess();
        proc::Process* target=nullptr;
        if(pid==-1){
            // get child procs
            // every child wakes up parent at exit?
            auto childs=kGlobObjs.procMgr.getChilds(curproc->pid());
            while(!childs.empty()){
                for(auto child:childs){
                    if(child->state==sched::Zombie){
                        target=child;
                        break;
                    }
                }
                if(target)break;
                sleep();
                childs=kGlobObjs.procMgr.getChilds(curproc->pid());
            }
        }
        else if(pid>0){
            auto proc=kGlobObjs.procMgr[pid];
            while(proc->state!=sched::Zombie){
                // proc add hook
                sleep();
            }
        }
        else if(pid==0)panic("waitpid: unimplemented!");
        else if(pid<0)panic("waitpid: unimplemented!");
        if(target==nullptr)return statcode::err;
        auto rt=target->pid();
        if(wstatus)kHartObjs.curtask->getProcess()->vmar[wstatus]=(int)(target->exitstatus<<8);
        target->zombieExit();
        return rt;
    }
    int wait(){
        auto &cur=kHartObjs.curtask;
        auto &ctx=cur->ctx;
        pid_t pid=ctx.x(10);
        xlen_t wstatus=ctx.x(11);
        return waitpid(pid,wstatus,0);
    }
    void init(){
        using sys::syscalls;
        syscallPtrs[syscalls::none]=none;
        syscallPtrs[syscalls::testexit]=testexit;
        syscallPtrs[syscalls::testyield]=sysyield;
        syscallPtrs[syscalls::testwrite]=write;
        // syscallPtrs[SYS_getcwd] = sys_getcwd;
        // syscallPtrs[SYS_dup] = sys_dup;
        // syscallPtrs[SYS_dup3] = sys_dup3;
        // syscallPtrs[SYS_fcntl] = sys_fcntl;
        // syscallPtrs[SYS_ioctl] = sys_ioctl;
        // syscallPtrs[SYS_flock] = sys_flock;
        // syscallPtrs[SYS_mknodat] = sys_mknodat;
        // syscallPtrs[SYS_mkdirat] = sys_mkdirat;
        // syscallPtrs[SYS_unlinkat] = sys_unlinkat;
        // syscallPtrs[SYS_symlinkat] = sys_symlinkat;
        // syscallPtrs[SYS_linkat] = sys_linkat;
        // syscallPtrs[SYS_renameat] = sys_renameat;
        // syscallPtrs[SYS_umount2] = sys_umount2;
        // syscallPtrs[SYS_mount] = sys_mount;
        // syscallPtrs[SYS_pivot_root] = sys_pivot_root;
        // syscallPtrs[SYS_nfsservctl] = sys_nfsservctl;
        // syscallPtrs[SYS_statfs] = sys_statfs;
        // syscallPtrs[SYS_fstatfs] = sys_fstatfs;
        // syscallPtrs[SYS_truncate] = sys_truncate;
        // syscallPtrs[SYS_ftruncate] = sys_ftruncate;
        // syscallPtrs[SYS_fallocate] = sys_fallocate;
        // syscallPtrs[SYS_faccessat] = sys_faccessat;
        // syscallPtrs[SYS_chdir] = sys_chdir;
        // syscallPtrs[SYS_fchdir] = sys_fchdir;
        // syscallPtrs[SYS_chroot] = sys_chroot;
        // syscallPtrs[SYS_fchmod] = sys_fchmod;
        // syscallPtrs[SYS_fchmodat] = sys_fchmodat;
        // syscallPtrs[SYS_fchownat] = sys_fchownat;
        // syscallPtrs[SYS_fchown] = sys_fchown;
        // syscallPtrs[SYS_openat] = sys_openat;
        // syscallPtrs[SYS_close] = sys_close;
        // syscallPtrs[SYS_vhangup] = sys_vhangup;
        syscallPtrs[syscalls::pipe2] = pipe2;
        // syscallPtrs[SYS_quotactl] = sys_quotactl;
        // syscallPtrs[SYS_getdents64] = sys_getdents64;
        // syscallPtrs[SYS_lseek] = sys_lseek;
        syscallPtrs[syscalls::dup] = syscall::dup;
        syscallPtrs[syscalls::dup3] = syscall::dup3;
        syscallPtrs[syscalls::openat] = syscall::openAt;
        syscallPtrs[syscalls::close] = syscall::close;
        syscallPtrs[syscalls::read] = read;
        syscallPtrs[syscalls::write] = syscall::write;
        syscallPtrs[syscalls::exit] = syscall::exit;
        syscallPtrs[syscalls::yield] = syscall::sysyield;
        syscallPtrs[syscalls::getpid] = syscall::getPid;
        syscallPtrs[syscalls::clone] = syscall::clone;
        syscallPtrs[syscalls::wait] = syscall::wait;
    }
} // namespace syscall
