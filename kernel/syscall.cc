#include "kernel.hh"
#include "sched.hh"
#include "stat.h"
#include "fat.hh"

syscall_t syscallPtrs[sys::syscalls::nSyscalls];
extern void _strapexit();
namespace syscall
{
    using sys::statcode;
    using klib::SharedPtr;
    // using klib::make_shared;
    xlen_t none(){return 0;}
    xlen_t testexit(){
        static bool b=false;
        b=!b;
        if(b)return 1;
        return -1;
    }
    xlen_t getCwd(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        char *a_buf = (char*)ctx.x(10);
        size_t a_len = ctx.x(11);
        if(a_buf == nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        struct fs::dirent *de = curproc->cwd;
        char path[FAT32_MAX_PATH];
        char *s;
        // todo: 路径处理过程考虑包装成类
        if (de->parent == nullptr) { s = "/"; } // s为字符串指针，必须指向双引号字符串"/"
        else {
            s = path + FAT32_MAX_PATH - 1;
            *s = '\0';
            for(size_t len;de->parent != nullptr;) {
                len = strlen(de->filename);
                s -= len;
                if (s <= path) { return statcode::err; } // can't reach root '/'
                strncpy(s, de->filename, len);
                *(--s) = '/';
                de = de->parent;
            }
        }
        // todo: 内存相关
        // if (copyout2(a_buf, s, strlen(s)+1) < 0) { return nullptr; }
        return (xlen_t)a_buf;
    }
    inline bool fdOutRange(int a_fd) { return (a_fd<0) || (a_fd>proc::MaxOpenFile); }
    xlen_t dupArgsIn(int a_fd, int a_newfd=-1) {
        if(fdOutRange(a_fd)) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        SharedPtr<fs::File> f = curproc->files[a_fd];
        if(f == nullptr) { return statcode::err; }
        // dupArgsIn内部newfd<0时视作由操作系统分配描述符（同fdAlloc），因此对newfd非负的判断应在外层dup3中完成
        int newfd = curproc->fdAlloc(f, a_newfd);
        if(fdOutRange(newfd)) { return statcode::err; }

        return newfd;
    }
    xlen_t dup() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);

        return dupArgsIn(a_fd);
    }
    xlen_t dup3() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);
        int a_newfd = ctx.x(11);
        if(fdOutRange(a_newfd)) { return statcode::err; }

        return dupArgsIn(a_fd, a_newfd);
    }
    xlen_t linkAt(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_olddirfd = ctx.x(10);
        const char *a_oldpath = (const char*)ctx.x(11);
        int a_newdirfd = ctx.x(12);
        const char *a_newpath = (const char*)ctx.x(13);
        int a_flags = ctx.x(14);
        if(fdOutRange(a_olddirfd) || fdOutRange(a_newdirfd) || a_oldpath==nullptr || a_newpath==nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        char oldpath[FAT32_MAX_PATH], newpath[FAT32_MAX_PATH];
         // todo: 是否能直接copy？
        strncpy(oldpath, a_oldpath, FAT32_MAX_PATH);
        strncpy(newpath, a_newpath, FAT32_MAX_PATH);
        SharedPtr<fs::File> f1, f2; // 初始化为nullptr
        if(*oldpath != '/') { f1 = curproc->files[a_olddirfd]; }
        if(*newpath != '/') { f2 = curproc->files[a_newdirfd]; }

        return fs::link(oldpath, f1, newpath, f2);
    }
    xlen_t chDir(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        const char *a_path = (const char*)ctx.x(10);
        if(a_path == nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        char path[FAT32_MAX_PATH];
        strncpy(path, a_path, FAT32_MAX_PATH); // todo: 是否能直接copy？
        struct fs::dirent *ep = fs::ename(path);
        if(ep == nullptr) { return statcode::err; }
        fs::elock(ep);
        if(!(ep->attribute & ATTR_DIRECTORY)){
            fs::eunlock(ep);
            fs::eput(ep);
            return statcode::err;
        }
        fs::eunlock(ep);

        fs::eput(curproc->cwd);
        curproc->cwd = ep;

        return statcode::ok;
    }
    xlen_t openAt() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_dirfd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        int a_flags = ctx.x(12);
        mode_t a_mode = ctx.x(13);
        if(fdOutRange(a_dirfd) || a_path==nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        char path[FAT32_MAX_PATH];
        strncpy(path, a_path, FAT32_MAX_PATH); // todo: 是否能这样copy？
        SharedPtr<fs::File> f1, f2;
        if(a_path[0] != '/') { f2 = curproc->files[a_dirfd]; }
        struct fs::dirent *ep;
        int fd;

        if(a_flags & O_CREATE) {
            ep = fs::create2(path, S_ISDIR(a_mode)?T_DIR:T_FILE, a_flags, f2);
            if(ep == nullptr) { return statcode::err; }
        }
        else {
            if((ep = fs::ename2(path, f2)) == nullptr) { return statcode::err; }
            fs::elock(ep);
            if((ep->attribute&ATTR_DIRECTORY) && ((a_flags&O_RDWR) || (a_flags&O_WRONLY))) {
                printf("dir can't write\n");
                fs::eunlock(ep);
                fs::eput(ep);
                return statcode::err;
            }
            if((a_flags&O_DIRECTORY) && !(ep->attribute&ATTR_DIRECTORY)) {
                printf("it is not dir\n");
                fs::eunlock(ep);
                fs::eput(ep);
                return statcode::err;
            }
        }
        f1 = new fs::File(ep, a_flags);
        f1->off = (a_flags&O_APPEND) ? ep->file_size : 0;
        fd = curproc->fdAlloc(f1);
        if(fdOutRange(fd)) {
            fs::eput(ep);
            return statcode::err;
            // 如果fd分配不成功，f过期后会自动delete        
        }
        if(!(ep->attribute&ATTR_DIRECTORY) && (a_flags&O_TRUNC)) { fs::etrunc(ep); }

        return fd;
    }
    xlen_t close() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);
        if(fdOutRange(a_fd)) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        if(curproc->files[a_fd] == nullptr) { return statcode::err; } // 不能关闭一个已关闭的文件
        // 不能用新的局部变量代替，局部变量和files[a_fd]是两个不同的SharedPtr
        curproc->files[a_fd].deRef();

        return statcode::ok;
    }
    xlen_t pipe2(){
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
    xlen_t getDents64(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);
        struct fs::dstat *a_buf = (struct fs::dstat*)ctx.x(11);
        size_t a_len = ctx.x(12);
        if(fdOutRange(a_fd) || a_buf==nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        SharedPtr<fs::File> f = curproc->files[a_fd];
        struct fs::dstat ds;
        size_t copylen = a_len<sizeof(ds)?a_len:sizeof(ds);
        if(f == nullptr) { return statcode::err; }
        getdstat(f->obj.ep, &ds);
        // todo: 内存相关
        // if(copyout2(buf, (char*)&ds, a_len<sizeof(ds)?a_len:sizeof(ds)) < 0) {
        //     printf("copy wrong\n");
        //     return statcode::err;
        // }

        return copylen;
    }
    xlen_t read(){
        auto &ctx=kHartObjs.curtask->ctx;
        int fd=ctx.x(10);
        xlen_t uva=ctx.x(11),len=ctx.x(12);
        auto file=kHartObjs.curtask->getProcess()->ofile(fd);
        file->read(uva,len);
        return statcode::ok;
    }
    xlen_t write(){
        auto &ctx=kHartObjs.curtask->ctx;
        xlen_t fd=ctx.x(10),uva=ctx.x(11),len=ctx.x(12);
        auto file=kHartObjs.curtask->getProcess()->ofile(fd);
        file->write(uva,len);
        return statcode::ok;
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
    xlen_t sysyield(){
        yield();
        return statcode::ok;
    }
    xlen_t getPid(){
        return kHartObjs.curtask->getProcess()->pid();
    }
    int sleep(){
        auto &cur=kHartObjs.curtask;
        cur->sleep();
        yield();
        return statcode::ok;
    }
    xlen_t clone(){
        auto &ctx=kHartObjs.curtask->ctx;
        proc::clone(kHartObjs.curtask);
        return statcode::ok;
    }
    void init(){
        using sys::syscalls;
        syscallPtrs[syscalls::none]=none;
        syscallPtrs[syscalls::testexit]=testexit;
        syscallPtrs[syscalls::testyield]=sysyield;
        syscallPtrs[syscalls::testwrite]=write;
        // syscallPtrs[SYS_fcntl] = sys_fcntl;
        // syscallPtrs[SYS_ioctl] = sys_ioctl;
        // syscallPtrs[SYS_flock] = sys_flock;
        // syscallPtrs[SYS_mknodat] = sys_mknodat;
        // syscallPtrs[SYS_mkdirat] = sys_mkdirat;
        // syscallPtrs[SYS_unlinkat] = sys_unlinkat;
        // syscallPtrs[SYS_symlinkat] = sys_symlinkat;
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
        // syscallPtrs[SYS_fchdir] = sys_fchdir;
        // syscallPtrs[SYS_chroot] = sys_chroot;
        // syscallPtrs[SYS_fchmod] = sys_fchmod;
        // syscallPtrs[SYS_fchmodat] = sys_fchmodat;
        // syscallPtrs[SYS_fchownat] = sys_fchownat;
        // syscallPtrs[SYS_fchown] = sys_fchown;
        // syscallPtrs[SYS_vhangup] = sys_vhangup;
        // syscallPtrs[SYS_quotactl] = sys_quotactl;
        // syscallPtrs[SYS_lseek] = sys_lseek;
        syscallPtrs[syscalls::getcwd] = syscall::getCwd;
        syscallPtrs[syscalls::dup] = syscall::dup;
        syscallPtrs[syscalls::dup3] = syscall::dup3;
        syscallPtrs[syscalls::linkat] = syscall::linkAt;
        syscallPtrs[syscalls::chdir] = syscall::chDir;
        syscallPtrs[syscalls::openat] = syscall::openAt;
        syscallPtrs[syscalls::close] = syscall::close;
        syscallPtrs[syscalls::pipe2] = syscall::pipe2;
        syscallPtrs[syscalls::getdents64] = syscall::getDents64;
        syscallPtrs[syscalls::read] = syscall::read;
        syscallPtrs[syscalls::write] = syscall::write;
        syscallPtrs[syscalls::yield] = syscall::sysyield;
        syscallPtrs[syscalls::getpid] = syscall::getPid;
        syscallPtrs[syscalls::clone] = syscall::clone;
    }
} // namespace syscall
