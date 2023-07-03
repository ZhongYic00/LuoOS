#include "kernel.hh"
#include "sched.hh"
#include "fat.hh"
#include "ld.hh"
#include "sbi.hh"
#include "TINYSTL/vector.h"
#include "linux/reboot.h"
#include "thirdparty/expected.hpp"
using nonstd::expected;

#define moduleLevel LogLevel::info

syscall_t syscallPtrs[sys::syscalls::nSyscalls];
extern void _strapexit();
extern char _uimg_start;
namespace syscall {
    using sys::statcode;
    using klib::SharedPtr;
    using kernel::TimeSpec;
    using kernel::UtSName;
    using fs::File;
    using fs::KStat;
    using fs::DStat;
    using fs::DirEnt;
    // 前向引用
    void yield();
    xlen_t none() { return 0; }
    xlen_t testExit() {
        static bool b = false;
        b =! b;
        if(b) { return statcode::ok; }
        return statcode::err;
    }
    xlen_t testBio() {
        // for(int i=0;i<300;i++){
        //     auto buf=bcache[{0,i%260}];
        // }
        return statcode::ok;
    }
    xlen_t testIdle() {
        return sleep();
    }
    xlen_t testMount() {
        int rt = fs::rootFSInit();
        kHartObjs.curtask->getProcess()->cwd = fs::Path("/").pathSearch();
        printf("0x%lx\n", kHartObjs.curtask->getProcess()->cwd);
        SharedPtr<File> f;
        // auto testfile = fs::pathCreateAt("/testfile", T_FILE, O_CREATE|O_RDWR, f);
        auto testfile = fs::Path("/testfile").pathCreate(T_FILE, O_CREATE|O_RDWR, f);
        assert(rt == 0);
        Log(info, "pathCreateAt success\n---------------------------------------------------------");
        klib::string content = "test write";
        rt=testfile->entWrite(false, (xlen_t)content.c_str(), 0, content.size());
        assert(rt == content.size());
        testfile->entRelse();
        Log(info, "entWrite success\n---------------------------------------------------------");
        testfile = fs::Path("/testfile").pathSearch(f);
        char buf[2 * content.size()];
        rt = testfile->entRead(false, (xlen_t)buf, 0, content.size());
        assert(rt == content.size());
        testfile->entRelse();
        Log(info, "entRead success\n---------------------------------------------------------");
        printf("%s\n", buf);
        return rt;
    }
    xlen_t testFATInit() {
        Log(info, "initializing fat\n");
        if(fs::rootFSInit() != 0) { panic("fat init failed\n"); }
        auto curproc = kHartObjs.curtask->getProcess();
        // curproc->cwd = fs::entEnter("/");
        curproc->cwd = fs::Path("/").pathSearch();
        curproc->files[3] = eastl::make_shared<File>(curproc->cwd,0);
        // DirEnt *ep = fs::pathCreate("/dev", T_DIR, 0);
        DirEnt *ep = fs::Path("/dev").pathCreate(T_DIR, 0);
        if(ep == nullptr) { panic("create /dev failed\n"); }
        // ep = fs::pathCreate("/dev/vda2", T_DIR, 0);
        ep = fs::Path("/dev/vda2").pathCreate(T_DIR, 0);
        // auto isempty = ep->isEmpty();
        // auto dp = fs::Path("/dev/vda2/test").pathCreate(T_DIR, 0);
        // isempty = ep->isEmpty();
        // fs::Path("/dev/vda2/test").pathRemove();
        // isempty = ep->isEmpty();
        if(ep == nullptr) { panic("create /dev/vda2 failed\n"); }
        Log(info,"fat initialize ok");
        return statcode::ok;
    }
    xlen_t getCwd(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        char *a_buf = (char*)ctx.x(10);
        size_t a_len = ctx.x(11);
        if(a_buf == nullptr) {
            // @todo 当a_buf == nullptr时改为由系统分配缓冲区
            a_len = 0;
            return statcode::err;
        }

        auto curproc = kHartObjs.curtask->getProcess();
        DirEnt *de = curproc->cwd;
        char path[FAT32_MAX_PATH];
        char *s;  // s为path的元素指针
        // @todo 路径处理过程考虑包装成类
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
        size_t len = strlen(s)+1;
        if(a_len < len)  { return NULL; }

        curproc->vmar.copyout((xlen_t)a_buf, klib::ByteArray((uint8_t*)s,len));

        return (xlen_t)a_buf;
    }
    inline bool fdOutRange(int a_fd) { return (a_fd<0) || (a_fd>proc::MaxOpenFile); }
    xlen_t dupArgsIn(int a_fd, int a_newfd=-1) {
        if(fdOutRange(a_fd)) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        SharedPtr<File> f = curproc->files[a_fd];
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
    xlen_t mkDirAt(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_dirfd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        mode_t a_mode = ctx.x(12); // @todo 还没用上
        if(a_dirfd == AT_FDCWD) { a_dirfd = 3; };
        if(fdOutRange(a_dirfd) || a_path == nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        klib::ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        SharedPtr<File> f;
        if(*path != '/') { f = curproc->files[a_dirfd]; }
        DirEnt *ep;

        // if((ep = fs::pathCreateAt(path, T_DIR, 0, f)) == nullptr) {
        ep = fs::Path(path).pathCreate(T_DIR, 0, f);
        if(ep == nullptr) {
            printf("can't create %s\n", path);
            return statcode::err;
        }
        ep->entRelse();

        return statcode::ok;
    }
    xlen_t unlinkAt(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_dirfd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        int a_flags = ctx.x(12); // 这玩意有什么用？
        if(a_dirfd == AT_FDCWD) { a_dirfd = 3; };
        if(fdOutRange(a_dirfd) || a_path==nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        klib::ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        SharedPtr<File> f;
        if(*path != '/') { f = curproc->files[a_dirfd]; } // 非绝对路径

        return fs::Path(path).pathUnlink(f);
    }
    xlen_t linkAt(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_olddirfd = ctx.x(10);
        const char *a_oldpath = (const char*)ctx.x(11);
        int a_newdirfd = ctx.x(12);
        const char *a_newpath = (const char*)ctx.x(13);
        int a_flags = ctx.x(14);
        if(a_olddirfd == AT_FDCWD) { a_olddirfd = 3; };
        if(a_newdirfd == AT_FDCWD) { a_newdirfd = 3; };
        if(fdOutRange(a_olddirfd) || fdOutRange(a_newdirfd) || a_oldpath==nullptr || a_newpath==nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        klib::ByteArray oldpatharr = curproc->vmar.copyinstr((xlen_t)a_oldpath, FAT32_MAX_PATH);
        klib::ByteArray newpatharr = curproc->vmar.copyinstr((xlen_t)a_newpath, FAT32_MAX_PATH);
        char *oldpath = (char*)oldpatharr.buff;
        char *newpath = (char*)newpatharr.buff;
        SharedPtr<File> f1, f2;
        if(*oldpath != '/') { f1 = curproc->files[a_olddirfd]; }
        if(*newpath != '/') { f2 = curproc->files[a_newdirfd]; }

        // return fs::entLink(oldpath, f1, newpath, f2);
        return fs::Path(oldpath).pathLink(f1, newpath, f2);
    }
    xlen_t umount2(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        const char *a_devpath = (const char*)ctx.x(10);
        int a_flags = ctx.x(11); // 没用上
        if(a_devpath == nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        klib::ByteArray devpatharr = curproc->vmar.copyinstr((xlen_t)a_devpath, FAT32_MAX_PATH);
        char *devpath = (char*)devpatharr.buff;
        if(strncmp("/",devpath,2) == 0) {
            printf("path error\n");
            return statcode::err;
        }
        // DirEnt *ep = fs::entEnter(devpath);
        DirEnt *ep = fs::Path(devpath).pathSearch();
        if(ep == nullptr) {
            printf("not found file\n");
            return statcode::err;
        }

        return ep->entUnmount();
    }
    xlen_t mount() {
        auto &ctx = kHartObjs.curtask->ctx;
        const char *a_devpath = (const char*)ctx.x(10);
        const char *a_mountpath = (const char*)ctx.x(11);
        const char *a_fstype = (const char*)ctx.x(12);
        xlen_t a_flags = ctx.x(13); // 没用上
        const void *a_data = (const void*)ctx.x(14); // 手册表示可为NULL
        if(a_devpath==nullptr || a_mountpath==nullptr || a_fstype==nullptr) { return statcode::err; }
        // 错误输出可以合并
        auto curproc = kHartObjs.curtask->getProcess();
        klib::ByteArray devpatharr = curproc->vmar.copyinstr((xlen_t)a_devpath, FAT32_MAX_PATH);
        klib::ByteArray mountpatharr = curproc->vmar.copyinstr((xlen_t)a_mountpath, FAT32_MAX_PATH);
        klib::ByteArray fstypearr = curproc->vmar.copyinstr((xlen_t)a_fstype, FAT32_MAX_PATH);
        char *devpath = (char*)devpatharr.buff;
        char *mountpath = (char*)mountpatharr.buff;
        char *FileSystem = (char*)fstypearr.buff;
        if(strncmp("/",mountpath,2) == 0) { //mountpoint not allowed the root
            printf("not allowed\n");
            return statcode::err;
        }
        if ((strncmp("vfat",FileSystem,5)!=0) && (strncmp("fat32",FileSystem,6)!=0)) {
            printf("the FileSystem is not fat32\n");
            return statcode::err;
        }

        // DirEnt *dev_ep = fs::entEnter(devpath);
        // DirEnt *ep = fs::entEnter(mountpath);
        DirEnt *dev_ep = fs::Path(devpath).pathSearch();
        DirEnt *ep = fs::Path(mountpath).pathSearch();
        if(dev_ep == nullptr) {
            printf("dev not found file\n");
            return statcode::err;
        }
        if(ep == nullptr) {
            printf("mount not found file\n");
            return statcode::err;
        }
        if(!(ep->attribute & ATTR_DIRECTORY)) {
            printf("mountpoint is not a dir\n");
            return statcode::err;
        }

        return ep->entMount(dev_ep);
    }
    xlen_t chDir(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        const char *a_path = (const char*)ctx.x(10);
        if(a_path == nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        klib::ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        // DirEnt *ep = fs::entEnter(path);
        DirEnt *ep = fs::Path(path).pathSearch();
        if(ep == nullptr) { return statcode::err; }
        if(!(ep->attribute & ATTR_DIRECTORY)){
            ep->entRelse();
            return statcode::err;
        }

        curproc->cwd->entRelse();
        curproc->cwd = ep;
        curproc->files[3]=eastl::make_shared<File>(curproc->cwd, O_RDWR);

        return statcode::ok;
    }
    xlen_t openAt() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_dirfd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        int a_flags = ctx.x(12);
        mode_t a_mode = ctx.x(13);
        if(a_dirfd == AT_FDCWD) { a_dirfd = 3; };
        if(fdOutRange(a_dirfd) || a_path==nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        klib::ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        SharedPtr<File> f1, f2;
        if(path[0] != '/') { f2 = curproc->files[a_dirfd]; }
        DirEnt *ep;
        int fd;

        if(a_flags & O_CREATE) {
            // ep = fs::pathCreateAt(path, S_ISDIR(a_mode)?T_DIR:T_FILE, a_flags, f2);
            ep = fs::Path(path).pathCreate(S_ISDIR(a_mode)?T_DIR:T_FILE, a_flags, f2);
            if(ep == nullptr) { return statcode::err; }
        }
        else {
            // if((ep = fs::entEnterFrom(path, f2)) == nullptr) { return statcode::err; }
            if((ep = fs::Path(path).pathSearch(f2)) == nullptr) { return statcode::err; }
            if((ep->attribute&ATTR_DIRECTORY) && ((a_flags&O_RDWR) || (a_flags&O_WRONLY))) {
                printf("dir can't write\n");
                ep->entRelse();
                return statcode::err;
            }
            if((a_flags&O_DIRECTORY) && !(ep->attribute&ATTR_DIRECTORY)) {
                printf("it is not dir\n");
                ep->entRelse();
                return statcode::err;
            }
        }
        f1=eastl::make_shared<File>(ep, a_flags);
        f1->off = (a_flags&O_APPEND) ? ep->file_size : 0;
        fd = curproc->fdAlloc(f1);
        if(fdOutRange(fd)) {
            ep->entRelse();
            return statcode::err;
            // 如果fd分配不成功，f过期后会自动delete        
        }
        if(!(ep->attribute&ATTR_DIRECTORY) && (a_flags&O_TRUNC)) { ep->entTrunc(); }

        return fd;
    }
    xlen_t close() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);
        if(fdOutRange(a_fd)) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        if(curproc->files[a_fd] == nullptr) { return statcode::err; } // 不能关闭一个已关闭的文件
        // 不能用新的局部变量代替，局部变量和files[a_fd]是两个不同的SharedPtr
        curproc->files[a_fd].reset();

        return statcode::ok;
    }
    xlen_t pipe2(){
        auto &cur=kHartObjs.curtask;
        auto &ctx=cur->ctx;
        auto proc=cur->getProcess();
        xlen_t fd=ctx.x(10),flags=ctx.x(11);
        auto pipe=SharedPtr<pipe::Pipe>(new pipe::Pipe);
        auto rfile=SharedPtr<File>(new File(pipe,fs::FileOp::read));
        auto wfile=SharedPtr<File>(new File(pipe,fs::FileOp::write));
        int fds[]={proc->fdAlloc(rfile),proc->fdAlloc(wfile)};
        proc->vmar[fd]=fds;
    }
    xlen_t getDents64(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);
        DStat *a_buf = (DStat*)ctx.x(11);
        size_t a_len = ctx.x(12);
        if(fdOutRange(a_fd) || a_buf==nullptr || a_len<sizeof(DStat)) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        SharedPtr<File> f = curproc->files[a_fd];
        if(f == nullptr) { return statcode::err; }
        // DStat ds;
        // getDStat(f->obj.ep, &ds);
        DStat ds = *(f->obj.ep);
        curproc->vmar.copyout((xlen_t)a_buf, klib::ByteArray((uint8_t*)&ds,sizeof(ds)));

        return sizeof(ds);
    }
    xlen_t read(){
        auto &ctx=kHartObjs.curtask->ctx;
        int fd=ctx.x(10);
        xlen_t uva=ctx.x(11),len=ctx.x(12);
        auto file=kHartObjs.curtask->getProcess()->ofile(fd);
        auto bytes=file->read(len);
        kHartObjs.curtask->getProcess()->vmar[uva]=bytes;
        return bytes.len;
    }
    xlen_t write(){
        auto &ctx=kHartObjs.curtask->ctx;
        xlen_t fd=ctx.x(10),uva=ctx.x(11),len=ctx.x(12);
        auto file=kHartObjs.curtask->getProcess()->ofile(fd);
        return file->write(uva,len);
    }
    xlen_t exit(){
        auto status=kHartObjs.curtask->ctx.a0();
        kHartObjs.curtask->getProcess()->exit(status);
        yield();
    }
    xlen_t fStat() {
        auto &ctx = kHartObjs.curtask->ctx;
        int a_fd = ctx.x(10);
        KStat *a_kst = (KStat*)ctx.x(11);
        if(fdOutRange(a_fd) || a_kst==nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        SharedPtr<File> f = curproc->files[a_fd];
        if(f == nullptr) { return statcode::err; }
        // KStat kst;
        // fs::getKStat(f->obj.ep, &kst);
        KStat kst = *(f->obj.ep);
        curproc->vmar.copyout((xlen_t)a_kst, klib::ByteArray((uint8_t*)&kst,sizeof(kst)));
        // @bug 用户态读到的数据混乱
        return statcode::ok;
    }
    __attribute__((naked))
    void sleepSave(ptr_t gpr){
        saveContextTo(gpr);
        schedule();
        _strapexit(); //TODO check
    }
    xlen_t nanoSleep() {
        auto cur = kHartObjs.curtask;
        auto ctx = cur->ctx;
        TimeSpec *a_tv = (TimeSpec*)ctx.x(10);

        auto curproc = cur->getProcess();
        klib::ByteArray tvarray = curproc->vmar.copyin((xlen_t)a_tv, sizeof(TimeSpec));
        TimeSpec *tv = (TimeSpec*)tvarray.buff;
        struct proc::SleepingTask tosleep(cur, kHartObjs.g_ticks + tv->tvSec()*kernel::CLK_FREQ/kernel::INTERVAL + tv->tvNSec()*kernel::CLK_FREQ/(1000000*kernel::INTERVAL));
        for(int i = 0; i < kernel::NMAXSLEEP; ++i) {
            if(kHartObjs.sleep_tasks[i].m_task == nullptr) {
                kHartObjs.sleep_tasks[i] = tosleep;
                return sleep();
            }
        }
        return statcode::err;
    }
    void yield(){
        Log(debug,"yield!");
        auto &cur=kHartObjs.curtask;
        sleepSave(cur->kctx.gpr);
    }
    xlen_t sysyield(){
        yield();
        return statcode::ok;
    }
    xlen_t times(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        proc::Tms *a_tms = (proc::Tms*)ctx.x(10);
        // if(a_tms == nullptr) { return statcode::err; } // a_tms留空时不管tms只返回ticks？

        auto curproc = kHartObjs.curtask->getProcess();
        if(a_tms != nullptr) { curproc->vmar.copyout((xlen_t)a_tms, klib::ByteArray((uint8_t*)&curproc->ti, sizeof(proc::Tms))); }
        // acquire(&tickslock);
        int ticks = (int)(kHartObjs.g_ticks/kernel::INTERVAL);
        // release(&tickslock);

        return ticks;
    }
    xlen_t uName(void) {
        auto &ctx = kHartObjs.curtask->ctx;
        struct UtSName *a_uts = (struct UtSName*)ctx.x(10);
        if(a_uts == nullptr) { return statcode::err; }

        auto curproc = kHartObjs.curtask->getProcess();
        static struct UtSName uts = { "domainname", "machine", "nodename", "release", "sysname", "version" };
        curproc->vmar.copyout((xlen_t)a_uts, klib::ByteArray((uint8_t*)&uts, sizeof(uts)));
        // todo@ 这里是干些啥？
        // ExecInst(fence);
        // ExecInst(fence.i);
        // ExecInst(sfence.vma);
        // ExecInst(fence);
        // ExecInst(fence.i);

        return statcode::ok;
    }
    xlen_t getTimeOfDay() {
        auto &ctx = kHartObjs.curtask->ctx;
        TimeSpec *a_ts = (TimeSpec*)ctx.x(10);
        if(a_ts == nullptr) { return statcode::err; }
    
        auto curproc = kHartObjs.curtask->getProcess();
        xlen_t ticks;
        asm volatile("rdtime %0" : "=r" (ticks) );
        TimeSpec ts(ticks/kernel::CLK_FREQ, ((100000*ticks/kernel::CLK_FREQ)%100000)*10);
        curproc->vmar.copyout((xlen_t)a_ts, klib::ByteArray((uint8_t*)&ts, sizeof(ts)));

        return statcode::ok;
    }
    xlen_t getPid(){
        return kHartObjs.curtask->getProcess()->pid();
    }
    xlen_t getPPid() { return kHartObjs.curtask->getProcess()->parentProc()->pid(); }
    int sleep(){
        auto &cur=kHartObjs.curtask;
        cur->sleep();
        yield();
        return statcode::ok;
    }
    xlen_t clone(){
        auto &ctx=kHartObjs.curtask->ctx;
        xlen_t func=ctx.x(10),childStack=ctx.x(11);
        int flags=ctx.x(12);
        auto pid=proc::clone(kHartObjs.curtask);
        auto thrd=(**kGlobObjs->procMgr)[pid]->defaultTask();
        if(childStack)thrd->ctx.sp()=childStack;
        // if(func)thrd->ctx.pc=func;
        Log(debug,"clone curproc=%d, new proc=%d",kHartObjs.curtask->getProcess()->pid(),pid);
        return pid;
    }
    int waitpid(tid_t pid,xlen_t wstatus,int options){
        Log(debug,"waitpid(pid=%d,options=%d)",pid,options);
        auto curproc=kHartObjs.curtask->getProcess();
        proc::Process* target=nullptr;
        if(pid==-1){
            // get child procs
            // every child wakes up parent at exit?
            auto childs=kGlobObjs->procMgr->getChilds(curproc->pid());
            while(!childs.empty()){
                for(auto child:childs){
                    if(child->state==sched::Zombie){
                        target=child;
                        break;
                    }
                }
                if(target)break;
                sleep();
                childs=kGlobObjs->procMgr->getChilds(curproc->pid());
            }
        }
        else if(pid>0){
            auto proc=(**kGlobObjs->procMgr)[pid];
            while(proc->state!=sched::Zombie){
                // proc add hook
                sleep();
            }
            target=proc;
        }
        else if(pid==0)panic("waitpid: unimplemented!"); // @todo 每个回收的子进程都更新父进程的ti
        else if(pid<0)panic("waitpid: unimplemented!");
        if(target==nullptr)return statcode::err;
        curproc->ti += target->ti;
        auto rt=target->pid();
        if(wstatus)curproc->vmar[wstatus]=(int)(target->exitstatus<<8);
        target->zombieExit();
        return rt;
    }
    xlen_t wait(){
        auto &cur=kHartObjs.curtask;
        auto &ctx=cur->ctx;
        pid_t pid=ctx.x(10);
        xlen_t wstatus=ctx.x(11);
        return waitpid(pid,wstatus,0);
    }
    xlen_t brk(){
        auto &ctx=kHartObjs.curtask->ctx;
        auto &curproc=*kHartObjs.curtask->getProcess();
        xlen_t addr=ctx.x(10);
        return curproc.brk(addr);
    }
    xlen_t mmap(){
        auto &curproc=*kHartObjs.curtask->getProcess();
        auto &ctx=kHartObjs.curtask->ctx;
        xlen_t addr=ctx.x(10),len=ctx.x(11);
        int prot=ctx.x(12),flags=ctx.x(13),fd=ctx.x(14),offset=ctx.x(15);
        using namespace vm;
        // determine target
        const auto align=[](xlen_t addr){return addr2pn(addr);};
        const auto choose=[&](){
            auto rt=curproc.heapTop=ceil(curproc.heapTop);
            curproc.heapTop+=len;
            curproc.heapTop=ceil(curproc.heapTop);
            return addr2pn(rt);
        };
        PageNum vpn,pages;
        if(addr)vpn=align(addr);
        else vpn=choose();
        pages = bytes2pages(len);
        // initialize vmo
        auto vmo=VMO::alloc(pages);
        /// @todo register for shared mapping
        /// @todo copy content to vmo
        if(fd!=-1){
            auto file=curproc.ofile(fd);
            auto bytes=file->read(len, 0, false);
            memmove((ptr_t)pn2addr(vmo.ppn()),bytes.buff,bytes.len);
        }
        // actual map
        auto mappingType= fd==-1 ?PageMapping::MappingType::file : PageMapping::MappingType::anon;
        auto sharingType=(PageMapping::SharingType)(flags>>8);
        curproc.vmar.map(PageMapping{vpn,vmo,PageMapping::prot2perm((PageMapping::Prot)prot),mappingType,sharingType});
        // return val
        return pn2addr(vpn);
    }
    xlen_t munmap(){
        auto &ctx=kHartObjs.curtask->ctx;
        auto &curproc=*kHartObjs.curtask->getProcess();
        xlen_t addr=ctx.x(10),len=ctx.x(11);
        /// @todo len, partial unmap?
        using namespace vm;
        if(addr&vaddrOffsetMask){
            Log(warning,"munmap addr not aligned!");
            return statcode::err;
        }
        addr=addr2pn(addr);
        auto mapping=curproc.vmar.find([&](const PageMapping &mapping){
            /// @todo addr in mapping?
            return mapping.vpn==addr;
        });
        curproc.vmar.unmap(mapping);
    }
    // xlen_t mprotect(){}
    int execve(klib::ByteArray buf,tinystl::vector<klib::ByteArray> &args,char **envp){
        auto &ctx=kHartObjs.curtask->ctx;
        /// @todo reset cur proc vmar, refer to man 2 execve for details
        kHartObjs.curtask->getProcess()->vmar.reset();
        /// @todo destroy other threads
        /// @todo reset curtask cpu context
        ctx=proc::Context();
        // static_cast<proc::Context>(kHartObjs.curtask->kctx)=proc::Context();
        ctx.sp()=proc::UserStackDefault;
        /// load elf
        ctx.pc=
            ld::loadElf(buf.buff,kHartObjs.curtask->getProcess()->vmar);
        /// setup stack
        auto &vmar=kHartObjs.curtask->getProcess()->vmar;
        klib::ArrayBuff<xlen_t> argv(args.size()+1);
        int argc=0;
        for(auto arg:args){
            ctx.sp()-=arg.len;
            vmar[ctx.sp()]=arg;
            argv.buff[argc++]=ctx.sp();
        }
        argv.buff[argc]=0;
        ctx.sp()-=argv.len*sizeof(xlen_t);
        ctx.sp()-=ctx.sp()%16;
        auto argvsp=ctx.sp();
        vmar[ctx.sp()]=argv.toArrayBuff<uint8_t>();
        /// @bug alignment?
        ctx.sp()-=8;
        auto argcsp=ctx.sp();
        vmar[ctx.sp()]=argc;
        Log(debug,"$sp=%x, argc@%x, argv@%x",ctx.sp(),argcsp,argvsp);
        /// setup argc, argv
        return ctx.sp();
    }
    xlen_t execve(){
        auto &cur=kHartObjs.curtask;
        auto &ctx=cur->ctx;
        auto curproc=cur->getProcess();
        xlen_t pathuva=ctx.x(10),argv=ctx.x(11),envp=ctx.x(12);

        /// @brief get executable from path
        klib::ByteArray pathbuf = curproc->vmar.copyinstr(pathuva, FAT32_MAX_PATH);
        // klib::string path((char*)pathbuf.buff,pathbuf.len);
        char *path=(char*)pathbuf.buff;

        Log(debug,"execve(path=%s,)",path);
        // auto Ent=fs::entEnter(path);
        auto Ent=fs::Path(path).pathSearch();
        auto file=eastl::make_shared<File>(Ent,fs::FileOp::read);
        auto buf=file->read(Ent->file_size);
        // auto buf=klib::ByteArray{0};
        // buf.buff=(uint8_t*)((xlen_t)&_uimg_start);buf.len=0x3ba0000;

        /// @brief get args
        tinystl::vector<klib::ByteArray> args;
        xlen_t str;
        do{
            curproc->vmar[argv]>>str;
            if(!str)break;
            auto buff=curproc->vmar.copyinstr(str,100);
            args.push_back(buff);
            argv+=sizeof(char*);
        }while(str!=0);
        /// @brief get envs
        return execve(buf,args,0);
    }
    expected<xlen_t,tinystl::string> reboot(){
        auto &ctx=kHartObjs.curtask->ctx;
        int magic=ctx.x(10),magic2=ctx.x(11),cmd=ctx.x(12);
        if(!(magic==LINUX_REBOOT_MAGIC1 && magic2==LINUX_REBOOT_MAGIC2))return nonstd::make_unexpected("magic num unmatched!");
        if(cmd==LINUX_REBOOT_CMD_POWER_OFF){
            Log(error,"LuoOS Shutdown! Bye-Bye");
            sbi_shutdown();
        }
    }
    xlen_t reboot1(){
        if(auto ret=reboot()){
            return ret.value();
        } else {
            Log(warning,"syscall failed, reason: %s",ret.error().c_str());
            return statcode::err;
        }
    }
    void init(){
        using sys::syscalls;
        syscallPtrs[syscalls::none]=none;
        syscallPtrs[syscalls::testexit]=testExit;
        syscallPtrs[syscalls::testyield]=sysyield;
        syscallPtrs[syscalls::testwrite]=write;
        syscallPtrs[syscalls::testbio]=testBio;
        syscallPtrs[syscalls::testidle]=testIdle;
        syscallPtrs[syscalls::testmount]=testMount;
        syscallPtrs[syscalls::testfatinit]=testFATInit;
        syscallPtrs[syscalls::reboot]=reboot1;
        // syscallPtrs[SYS_fcntl] = sys_fcntl;
        // syscallPtrs[SYS_ioctl] = sys_ioctl;
        // syscallPtrs[SYS_flock] = sys_flock;
        // syscallPtrs[SYS_mknodat] = sys_mknodat;
        // syscallPtrs[SYS_symlinkat] = sys_symlinkat;
        // syscallPtrs[SYS_renameat] = sys_renameat;
        // syscallPtrs[SYS_pivot_root] = sys_pivot_root;
        // syscallPtrs[SYS_nfsservctl] = sys_nfsservctl;
        // syscallPtrs[SYS_statfs] = sys_statfs;
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
        syscallPtrs[syscalls::mkdirat] = syscall::mkDirAt;
        syscallPtrs[syscalls::linkat] = syscall::linkAt;
        syscallPtrs[syscalls::unlinkat] = syscall::unlinkAt;
        syscallPtrs[syscalls::umount2] = syscall::umount2;
        syscallPtrs[syscalls::mount] = syscall::mount;
        syscallPtrs[syscalls::chdir] = syscall::chDir;
        syscallPtrs[syscalls::openat] = syscall::openAt;
        syscallPtrs[syscalls::close] = syscall::close;
        syscallPtrs[syscalls::pipe2] = syscall::pipe2;
        syscallPtrs[syscalls::getdents64] = syscall::getDents64;
        syscallPtrs[syscalls::read] = syscall::read;
        syscallPtrs[syscalls::write] = syscall::write;
        syscallPtrs[syscalls::exit] = syscall::exit;
        syscallPtrs[syscalls::fstat] = syscall::fStat;
        syscallPtrs[syscalls::nanosleep] = syscall::nanoSleep;
        syscallPtrs[syscalls::yield] = syscall::sysyield;
        syscallPtrs[syscalls::times] = syscall::times;
        syscallPtrs[syscalls::uname] = syscall::uName;
        syscallPtrs[syscalls::gettimeofday] = syscall::getTimeOfDay;
        syscallPtrs[syscalls::getpid] = syscall::getPid;
        syscallPtrs[syscalls::getppid] = syscall::getPPid;
        syscallPtrs[syscalls::brk] = syscall::brk;
        syscallPtrs[syscalls::munmap] = syscall::munmap;
        syscallPtrs[syscalls::clone] = syscall::clone;
        syscallPtrs[syscalls::execve] = syscall::execve;
        syscallPtrs[syscalls::mmap] = syscall::mmap;
        syscallPtrs[syscalls::wait] = syscall::wait;
    }
} // namespace syscall
