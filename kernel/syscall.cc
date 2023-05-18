#include "kernel.hh"
#include "sched.hh"

syscall_t syscallPtrs[sys::syscalls::nSyscalls];
extern void _strapexit();
namespace syscall
{
    using sys::statcode;
    int none(){return 0;}
    int testexit(){
        static bool b=false;
        b=!b;
        if(b)return 1;
        return -1;
    }
    int write(){
        auto &ctx=kHartObjs.curtask->ctx;
        xlen_t fd=ctx.x(10),uva=ctx.x(11),len=ctx.x(12);
        auto file=kHartObjs.curtask->getProcess()->ofile(fd);
        file->write(uva,len);
    }
    __attribute__((naked))
    void sleepSave(ptr_t gpr){
        saveContextTo(gpr);
        schedule();
        _strapexit(); //TODO check
    }
    void yield(){
        auto &cur=kHartObjs.curtask;
        cur->lastpriv=proc::Task::Priv::Kernel;
        sleepSave(cur->kctx.gpr);
    }
    int sysyield(){
        printf("syscall yield\n");
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
        proc::clone(kHartObjs.curtask);
        return statcode::ok;
    }
    int openAt(){
        auto &ctx = kHartObjs.curtask->ctx;
        int dirfd = ctx.x(10);
        const char *path = (const char*)ctx.x(11);
        int flags = ctx.x(12);
        mode_t mode = ctx.x(13); // uint32

        int fd;
        fs::File *f;
        fs::INode *in;
        auto curproc = kHartObjs.curtask->getProcess();

        /*
            inode相关操作
        */
        // 测试用
        in = new fs::INode;
        in->type = fs::INode::file;
        //

        f = new fs::File;
        fd = curproc->fdAlloc(f);
        if(fd < 0){
            return statcode::err;
        }

        if(in->type == fs::INode::dev){
            f->type = fs::File::dev;
        }
        else{
            f->type = fs::File::inode;
        }
        f->in = in;

        return fd;
    }
    int close(){
        auto &ctx = kHartObjs.curtask->ctx;
        int fd = ctx.x(10);

        fs::File *f;
        auto curproc = kHartObjs.curtask->getProcess();
        // 判断fd范围也许可以写成宏……
        if((fd<0) || (fd>proc::MaxOpenFile)){
            return statcode::err;
        }
        f = curproc->ofile(fd);
        if(f == nullptr){
            return statcode::err;
        }

        f->fileClose();
        f = nullptr;
        return statcode::ok;
    }
    int dupArgsIn(int fd, int newfd=-1){
        fs::File *f;
        auto curproc = kHartObjs.curtask->getProcess();
        // 判断fd范围也许可以写成宏……
        if((fd<0) || (fd>proc::MaxOpenFile)){
            return statcode::err;
        }
        
        f = curproc->ofile(fd);
        if(f == nullptr){
            return statcode::err;
        }
        // dupArgsIn内部newfd<0时视作由操作系统分配描述符（同fdAlloc），因此对newfd非负的判断应在外层dup3中完成
        newfd = curproc->fdAlloc(f, newfd);
        if(newfd < 0){
            return statcode::err;
        }

        return newfd;
    }
    int dup(){
        auto &ctx = kHartObjs.curtask->ctx;
        int fd = ctx.x(10);

        return dupArgsIn(fd);
    }
    int dup3(){
        auto &ctx = kHartObjs.curtask->ctx;
        int fd = ctx.x(10);
        int newfd = ctx.x(11);
        // 判断fd范围也许可以写成宏……
        if((newfd<0) || (newfd>proc::MaxOpenFile)){
            return statcode::err;
        }

        return dupArgsIn(fd, newfd);
    }
    int pipe2(){
        auto &ctx=kHartObjs.curtask->ctx;
        xlen_t fd=ctx.x(10),flags=ctx.x(11);
        
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
        // syscallPtrs[SYS_read] = sys_read;
        syscallPtrs[syscalls::dup] = syscall::dup;
        syscallPtrs[syscalls::dup3] = syscall::dup3;
        syscallPtrs[syscalls::openat] = syscall::openAt;
        syscallPtrs[syscalls::close] = syscall::close;
        syscallPtrs[syscalls::write] = syscall::write;
        syscallPtrs[syscalls::yield] = syscall::sysyield;
        syscallPtrs[syscalls::getpid] = syscall::getPid;
        syscallPtrs[syscalls::clone] = syscall::clone;
    }
} // namespace syscall
