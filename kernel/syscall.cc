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
        int fd=ctx.x(10),uva=ctx.x(11),len=ctx.x(12);
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
    }
    int openAt(){
        auto &ctx = kHartObjs.curtask->ctx;
        int dirfd = ctx.x(10);
        const char *path = (const char*)ctx.x(11);
        int flags = ctx.x(12);
        word_t mode = ctx.x(13); // word_t应该实际为mode_t等定义

        int fd;
        fs::File *f;
        fs::INode *in;

        /*
            inode相关操作
        */
        // 测试用
        in = new fs::INode;
        in->type = fs::INode::file;
        //

        f = new fs::File;
        fd = kHartObjs.curtask->getProcess()->fdAlloc(f);
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
    int sysclose(){
        auto &ctx = kHartObjs.curtask->ctx;
        int fd = ctx.x(10);

        fs::File *f;

        if((fd<0) || (fd>proc::MaxOpenFile)){
            return statcode::err;
        }
        f = kHartObjs.curtask->getProcess()->files[fd];
        if(f == nullptr){
            return statcode::err;
        }

        f->fileClose();
        f = nullptr;
        return statcode::ok;
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
        // syscallPtrs[SYS_pipe2] = sys_pipe2;
        // syscallPtrs[SYS_quotactl] = sys_quotactl;
        // syscallPtrs[SYS_getdents64] = sys_getdents64;
        // syscallPtrs[SYS_lseek] = sys_lseek;
        // syscallPtrs[SYS_read] = sys_read;
        syscallPtrs[syscalls::write] = write;
        syscallPtrs[syscalls::yield] = sysyield;
        syscallPtrs[syscalls::getpid] = getPid;
        syscallPtrs[syscalls::clone] = clone;
        syscallPtrs[syscalls::openat] = openAt;
        syscallPtrs[syscalls::close] = sysclose;
    }
} // namespace syscall
