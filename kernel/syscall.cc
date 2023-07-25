#include "kernel.hh"
#include "sched.hh"
#include "fs.hh"
#include "ld.hh"
#include "sbi.hh"
#include "linux/reboot.h"
#include "thirdparty/expected.hpp"
#include <EASTL/chrono.h>
#include "bio.hh"
#include "vm/vmo.hh"
using nonstd::expected;

#define moduleLevel LogLevel::info

syscall_t syscallPtrs[sys::syscalls::nSyscalls];
extern void _strapexit();
extern char _uimg_start;
namespace syscall {
    using sys::statcode;
    using kernel::UtSName;
    using fs::File;
    using fs::Path;
    using fs::KStat;
    using fs::DStat;
    using fs::DEntry;
    using fs::StatFS;
    using proc::fdOutRange;
    using proc::FdCwd;
    using signal::SignalAction;
    using signal::SigSet;
    using signal::SignalInfo;
    using signal::send;
    using signal::doSigAction;
    using signal::doSigProcMask;
    using signal::doSigReturn;
    using resource::RLim;
    using resource::RSrc;
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
        for(int i=0;i<300;i++){
            auto buf=bcache[{0,i%260}];
        }
        return statcode::ok;
    }
    xlen_t testIdle() {
        return sleep();
    }
    xlen_t testMount() {
        // int rt = fs::rootFSInit();
        // kHartObj().curtask->getProcess()->cwd = Path("/").pathSearch();
        // printf("0x%lx\n", kHartObj().curtask->getProcess()->cwd);
        // shared_ptr<File> f;
        // // auto testfile = fs::pathCreateAt("/testfile", T_FILE, O_CREAT|O_RDWR, f);
        // auto testfile = Path("/testfile").pathCreate(T_FILE, O_CREAT|O_RDWR, f);
        // assert(rt == 0);
        // Log(info, "pathCreateAt success\n---------------------------------------------------------");
        // string content = "test write";
        // rt=testfile->entWrite(false, (xlen_t)content.c_str(), 0, content.size());
        // assert(rt == content.size());
        // testfile->entRelse();
        // Log(info, "entWrite success\n---------------------------------------------------------");
        // testfile = Path("/testfile").pathSearch(f);
        // char buf[2 * content.size()];
        // rt = testfile->entRead(false, (xlen_t)buf, 0, content.size());
        // assert(rt == content.size());
        // testfile->entRelse();
        // Log(info, "entRead success\n---------------------------------------------------------");
        // printf("%s\n", buf);
        // return rt;
        return statcode::ok;
    }
    xlen_t testFATInit() {
        Log(info, "initializing fat\n");
        int init = fs::rootFSInit();
        if(init != 0) { panic("fat init failed\n"); }
        auto curproc = kHartObj().curtask->getProcess();
        // curproc->cwd = fs::entEnter("/");
        curproc->cwd = Path("/").pathSearch();
        curproc->files[FdCwd] = make_shared<File>(curproc->cwd,0);
        // DirEnt *ep = fs::pathCreate("/dev", T_DIR, 0);
        shared_ptr<DEntry> ep = Path("/dev").pathCreate(T_DIR, 0);
        if(ep == nullptr) { panic("create /dev failed\n"); }
        // ep = fs::pathCreate("/dev/vda2", T_DIR, 0);
        ep = Path("/dev/vda2").pathCreate(T_DIR, 0);
        if(ep == nullptr) { panic("create /dev/vda2 failed\n"); }
        Log(info,"fat initialize ok");
        return statcode::ok;
    }
    xlen_t getCwd(void) {
        auto &ctx = kHartObj().curtask->ctx;
        char *a_buf = (char*)ctx.x(10);
        size_t a_len = ctx.x(11);
        if(a_buf == nullptr) {
            // @todo 当a_buf == nullptr时改为由系统分配缓冲区
            a_len = 0;
            return statcode::err;
        }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<DEntry> base = curproc->cwd;
        string pathcwd = Path(base).pathAbsolute();
        const char *strcwd = pathcwd.c_str();
        
        size_t len = pathcwd.size() + 1;
        if(a_len < len)  { return NULL; }
        curproc->vmar.copyout((xlen_t)a_buf, ByteArray((uint8*)strcwd, len));
        return (xlen_t)a_buf;
    }
    xlen_t dup() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return curproc->fdAlloc(file);
    }
    xlen_t dup3() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        int a_newfd = ctx.x(11);
        int a_flags = ctx.x(12);
        if(a_fd == a_newfd) { return -EINVAL; }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        int newfd = curproc->fdAlloc(file, a_newfd, true);
        if(newfd >= 0) {
            curproc->ofile(newfd)->flags &= ~O_CLOEXEC;
            curproc->ofile(newfd)->flags |= a_flags;
        }
        return newfd;
    }
    xlen_t fCntl() {

        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        int a_cmd = ctx.x(11);
        uint64 a_arg = ctx.x(12);
        
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }
        switch(a_cmd) {
            case F_DUPFD: { return curproc->fdAlloc(file, a_arg); }
            // NOTE: file descripter flags and file status flags are not the same
            // GET/SETFD asked for file descripter flags
            // since file descripter flags only have FD_CLOEXEC, we can just return 0 or FD_CLOEXEC
            // and we dont use another field to store file descripter flags
            // so we convert O_CLOEXEC to FD_CLOEXEC here
            case F_GETFD: { return (file->flags&O_CLOEXEC) ? FD_CLOEXEC : 0; }
            case F_SETFD: {
                // NOTE: see NOTE in F_GETFD, we convert FD_CLOEXEC to O_CLOEXEC here
                if (a_arg & FD_CLOEXEC) { file->flags |= O_CLOEXEC; }
                else { file->flags &= ~O_CLOEXEC; }
                return 0;
            }
            case F_GETFL: { return file->flags; }
            // NOTE: F_SETFL can change only O_APPEND, O_ASYNC, O_DIRECT, O_NOATIME, and O_NONBLOCK
            case F_SETFL: {
                file->flags &= ~(O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK);
                a_arg &= (O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK);
                if (a_arg & (O_ASYNC|O_DIRECT|O_NOATIME|O_NONBLOCK)) { Log(error, "fcntl: flags(%d) not supported\n", a_arg); }
                file->flags |= a_arg;
                return 0;
            }
            case F_DUPFD_CLOEXEC: {
                int newfd = curproc->fdAlloc(file, a_arg);
                if(newfd >= 0) {
                    curproc->ofile(newfd)->flags &= ~O_CLOEXEC;
                    curproc->ofile(newfd)->flags |= O_CLOEXEC;
                }
                return newfd;
            }
            default: {
                Log(error, "fcntl: unimplemented cmd %d\n", a_cmd);
                return -EINVAL;
            }
        }

        return -EINVAL;  // should never reach here
    }
    xlen_t mkDirAt(void) {
        auto &ctx = kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        mode_t a_mode = ctx.x(12); // @todo 还没用上
        if(a_path == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }
        shared_ptr<DEntry> entry = Path(path, base).pathCreate(T_DIR, 0);
        if(entry == nullptr) {
            printf("can't create %s\n", path);
            return statcode::err;
        }

        return statcode::ok;
    }
    xlen_t unlinkAt(void) {
        auto &ctx = kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        int a_flags = ctx.x(12); // 这玩意有什么用？
        if(a_path == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        return Path(path, base).pathHardUnlink();
    }
    xlen_t symLinkAt() {
        auto &ctx = kHartObj().curtask->ctx;
        const char *a_target = (const char*)ctx.x(10);
        int a_basefd = ctx.x(11);
        const char *a_linkpath = (const char*)ctx.x(13);

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray targetarr = curproc->vmar.copyinstr((xlen_t)a_target, FAT32_MAX_PATH);
        ByteArray linkarr = curproc->vmar.copyinstr((xlen_t)a_linkpath, FAT32_MAX_PATH);
        const char *target = (const char*)targetarr.buff;
        const char *link = (const char*)linkarr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        return Path(link, base).pathSymLink(target);
    }
    xlen_t linkAt(void) {
        auto &ctx = kHartObj().curtask->ctx;
        int a_oldbasefd = ctx.x(10);
        const char *a_oldpath = (const char*)ctx.x(11);
        int a_newbasefd = ctx.x(12);
        const char *a_newpath = (const char*)ctx.x(13);
        int a_flags = ctx.x(14);
        if(a_oldpath==nullptr || a_newpath==nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray oldpatharr = curproc->vmar.copyinstr((xlen_t)a_oldpath, FAT32_MAX_PATH);
        ByteArray newpatharr = curproc->vmar.copyinstr((xlen_t)a_newpath, FAT32_MAX_PATH);
        const char *oldpath = (const char*)oldpatharr.buff;
        const char *newpath = (const char*)newpatharr.buff;
        shared_ptr<File> oldbase = curproc->ofile(a_oldbasefd);
        shared_ptr<File> newbase = curproc->ofile(a_newbasefd);
        if(oldbase==nullptr || newbase==nullptr) { return -EBADF; }

        return Path(oldpath, oldbase).pathHardLink(Path(newpath, newbase));
    }
    xlen_t umount2(void) {
        auto &ctx = kHartObj().curtask->ctx;
        const char *a_devpath = (const char*)ctx.x(10);
        int a_flags = ctx.x(11); // 没用上
        if(a_devpath == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray devpatharr = curproc->vmar.copyinstr((xlen_t)a_devpath, FAT32_MAX_PATH);
        char *devpath = (char*)devpatharr.buff;
        return Path(devpath).pathUnmount();
    }
    xlen_t mount() {
        auto &ctx = kHartObj().curtask->ctx;
        const char *a_devpath = (const char*)ctx.x(10);
        const char *a_mountpath = (const char*)ctx.x(11);
        const char *a_fstype = (const char*)ctx.x(12);
        xlen_t a_flags = ctx.x(13); // 没用上
        const void *a_data = (const void*)ctx.x(14); // 手册表示可为NULL
        if(a_devpath==nullptr || a_mountpath==nullptr || a_fstype==nullptr) { return -EFAULT; }
        // 错误输出可以合并
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray devpatharr = curproc->vmar.copyinstr((xlen_t)a_devpath, FAT32_MAX_PATH);
        ByteArray mountpatharr = curproc->vmar.copyinstr((xlen_t)a_mountpath, FAT32_MAX_PATH);
        ByteArray fstypearr = curproc->vmar.copyinstr((xlen_t)a_fstype, FAT32_MAX_PATH);
        char *devpath = (char*)devpatharr.buff;
        char *mountpath = (char*)mountpatharr.buff;
        char *fstype = (char*)fstypearr.buff;
        return Path(mountpath).pathMount(devpath, fstype);
    }
    xlen_t statFS() {
        auto &ctx = kHartObj().curtask->ctx;
        const char *a_path = (const char*)ctx.x(10);
        StatFS *a_buf = (StatFS*)ctx.x(11);

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        const char *path = (const char*)patharr.buff;

        shared_ptr<DEntry> entry = Path(path).pathSearch();
        if(entry == nullptr) { return statcode::err; }
        StatFS stat = *(entry->getINode()->getSpBlk()->getFS());
        curproc->vmar.copyout((xlen_t)a_buf, ByteArray((uint8*)&stat, sizeof(stat)));

        return statcode::ok;
    }
    xlen_t fAccessAt() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        int a_mode = ctx.x(12);
        int a_flags = ctx.x(13);
        if(a_path == nullptr) { return -EFAULT; }

        using fs::F_OK;
        using fs::R_OK;
        using fs::W_OK;
        using fs::X_OK;

        int flags = 0;
        if ((a_mode & (R_OK | X_OK)) && (a_mode & W_OK)) { flags = O_RDWR; }
        else if (a_mode & W_OK) { flags = O_WRONLY; }
        else if (a_mode & (R_OK | X_OK)) { flags = O_RDONLY; }
        else { return -EINVAL; }
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        const char *path = (const char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        int fd = Path(path, base).pathOpen(flags | a_flags);
        if(fd<0 && fd!=-EISDIR) { return fd; }  // @todo: 存疑
        else if(fd >= 0) { curproc->files[fd].reset(); }
        return 0;
    }
    xlen_t chDir() {
        auto &ctx = kHartObj().curtask->ctx;
        const char *a_path = (const char*)ctx.x(10);
        if(a_path == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        // DirEnt *ep = fs::entEnter(path);
        shared_ptr<DEntry> ep = Path(path).pathSearch();
        if(ep == nullptr) { return statcode::err; }
        if(!(ep->getINode()->rAttr() & ATTR_DIRECTORY)){ return statcode::err; }

        curproc->cwd = ep;
        curproc->files[FdCwd] = make_shared<File>(curproc->cwd, O_RDWR);

        return statcode::ok;
    }
    xlen_t fChDir() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> nwd = curproc->ofile(a_fd);
        if(nwd == nullptr) { return -EBADF; }
        if(!(nwd->obj.ep->getINode()->rAttr() & ATTR_DIRECTORY)) { return -ENOTDIR; }
        curproc->cwd = nwd->obj.ep;
        curproc->files[FdCwd] = nwd;

        return statcode::ok;
    }
    xlen_t fChMod() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        mode_t a_mode = ctx.x(11);

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return file->chMod(a_mode);
    }
    xlen_t fChModAt() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        mode_t a_mode = ctx.x(12);
        int a_flags = ctx.x(13);

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_FILENAME);
        const char *path = (const char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        int fd = Path(path, base).pathOpen(a_flags, S_IFREG);

        if(fd < 0) { return fd; }  // 错误码
        int ret = curproc->ofile(fd)->chMod(a_mode);
        curproc->files[fd].reset();  // 关闭
        return ret;
    }
    xlen_t fChOwnAt() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        uid_t a_uid = ctx.x(12);
        gid_t a_gid = ctx.x(13);
        int a_flags = ctx.x(14);

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_FILENAME);
        const char *path = (const char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        int fd = Path(path, base).pathOpen(a_flags, S_IFREG);

        if(fd < 0) { return fd; }  // 错误码
        int ret = curproc->ofile(fd)->chOwn(a_uid, a_gid);
        curproc->files[fd].reset();  // 关闭
        return ret;
    }
    xlen_t fChOwn() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        uid_t a_uid = ctx.x(11);
        gid_t a_gid = ctx.x(12);

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return file->chOwn(a_uid, a_gid);
    }
    xlen_t openAt() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        int a_flags = ctx.x(12);
        mode_t a_mode = ctx.x(13);
        if(a_path==nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        return Path(path, base).pathOpen(a_flags, a_mode);
    }
    xlen_t close() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);

        auto curproc = kHartObj().curtask->getProcess();
        if(curproc->ofile(a_fd) == nullptr) { return -EBADF; } // 不能关闭一个已关闭的文件
        // 不能用新的局部变量代替，局部变量和files[a_fd]是两个不同的SharedPtr
        curproc->files[a_fd].reset();

        return statcode::ok;
    }
    xlen_t pipe2(){
        auto &cur=kHartObj().curtask;
        auto &ctx=cur->ctx;
        auto proc=cur->getProcess();
        xlen_t fd=ctx.x(10),flags=ctx.x(11);
        auto pipe=make_shared<pipe::Pipe>();
        auto rfile=make_shared<File>(pipe,fs::FileOp::read);
        auto wfile=make_shared<File>(pipe,fs::FileOp::write);
        int fds[]={proc->fdAlloc(rfile),proc->fdAlloc(wfile)};
        proc->vmar[fd]<<fds;
    }
    xlen_t getDents64(void) {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        DStat *a_buf = (DStat*)ctx.x(11);
        size_t a_len = ctx.x(12);
        if(a_buf==nullptr || a_len<sizeof(DStat)) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }
        DStat ds = file->obj.ep;
        curproc->vmar.copyout((xlen_t)a_buf, ByteArray((uint8*)&ds,sizeof(ds)));

        return sizeof(ds);
    }
    xlen_t lSeek() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        off_t a_offset = ctx.x(11);
        int a_whence = ctx.x(12);

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file = nullptr) { return -EBADF; }

        return file->lSeek(a_offset, a_whence);
    }
    xlen_t read(){
        auto &ctx=kHartObj().curtask->ctx;
        int fd = ctx.x(10);
        xlen_t uva = ctx.x(11), len = ctx.x(12);
        auto file = kHartObj().curtask->getProcess()->ofile(fd);
        if(file == nullptr) { return -EBADF; }
        auto bytes = file->read(len);
        kHartObj().curtask->getProcess()->vmar[uva] << bytes;
        return bytes.len;
    }
    xlen_t write(){
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        const char *a_src = (const char*)ctx.x(11);
        size_t a_len = ctx.x(12);
        if(a_src == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return file->write(curproc->vmar.copyin((xlen_t)a_src, a_len));
    }
    sysrt_t readv(int fd, xlen_t iov, int iovcnt);
    sysrt_t writev(int fd, xlen_t iov, int iovcnt);
    xlen_t exit(){
        auto cur=kHartObj().curtask;
        auto status=cur->ctx.a0();
        cur->getProcess()->exit(status);
        yield();
    }
    xlen_t exitGroup() {
        return exit();
    }
    xlen_t sendFile() {
        auto &ctx=kHartObj().curtask->ctx;
        int a_outfd = ctx.x(10);
        int a_infd = ctx.x(11);
        off_t *a_offset = (off_t*)ctx.x(12);
        size_t a_len = ctx.x(13);

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> outfile = curproc->ofile(a_outfd);
        shared_ptr<File> infile = curproc->ofile(a_infd);
        if(outfile==nullptr || infile==nullptr) { return -EBADF; }
        off_t *offset = nullptr;
        ByteArray offsetarr(sizeof(off_t));
        if(a_offset != nullptr) {
            offsetarr = curproc->vmar.copyin((xlen_t)a_offset, sizeof(off_t));
            offset = (off_t*)offsetarr.buff;
        }

        ssize_t ret = infile->sendFile(outfile, offset, a_len);
        if(a_offset != nullptr) { curproc->vmar.copyout((xlen_t)a_offset, offsetarr); }
        return ret;
    }
    xlen_t readLinkAt() {
        auto &ctx=kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        char *a_buf = (char*)ctx.x(12);
        size_t a_bufsiz = ctx.x(13);
        
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> base = curproc->ofile(a_basefd);
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        const char *path = (const char*)patharr.buff;

        int fd = Path(path, base).pathOpen(O_RDONLY, S_IFREG);
        if(fd < 0) { return fd; }
        ArrayBuff<char> bufarr(a_bufsiz);
        int ret = curproc->ofile(fd)->readLink(bufarr.buff, bufarr.len);
        curproc->files[fd].reset();
        curproc->vmar.copyout((xlen_t)a_buf, ByteArray((uint8*)bufarr.buff, a_bufsiz * sizeof(char)));

        return ret;
    }
    xlen_t fStatAt() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_basefd = ctx.x(10);
        const char *a_path = (const char*)ctx.x(11);
        KStat *a_kst = (KStat*)ctx.x(12);
        int a_flags = ctx.x(13);
        if(a_kst==nullptr || a_path==nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        const char *path = (const char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        shared_ptr<DEntry> entry = Path(path, base).pathSearch();
        if(entry == nullptr) { return statcode::err; }
        KStat kst = entry;
        curproc->vmar.copyout((xlen_t)a_kst, ByteArray((uint8*)&kst, sizeof(kst)));

        return statcode::ok;
    }
    xlen_t fStat() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        KStat *a_kst = (KStat*)ctx.x(11);
        if(a_kst==nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }
        KStat kst = file->obj.ep;
        curproc->vmar.copyout((xlen_t)a_kst, ByteArray((uint8*)&kst,sizeof(kst)));
        // @bug 用户态读到的数据混乱
        return statcode::ok;
    }
    xlen_t sync() {
        // 现阶段实现为write-through缓存，自动同步
        return statcode::ok;
    }
    __attribute__((naked))
    void sleepSave(ptr_t gpr){
        saveContextTo(gpr);
        auto curtask=kHartObj().curtask;
        curtask->kctx.pc=curtask->kctx.ra();
        curtask->kctxs.push(curtask->kctx);
        schedule();
        _strapexit(); //TODO check
    }
    xlen_t nanoSleep() {
        auto cur = kHartObj().curtask;
        auto ctx = cur->ctx;
        struct timespec *a_tv = (struct timespec*)ctx.x(10);

        auto curproc = cur->getProcess();
        ByteArray tvarray = curproc->vmar.copyin((xlen_t)a_tv, sizeof(struct timespec));
        struct timespec *tv = (struct timespec*)tvarray.buff;
        struct proc::SleepingTask tosleep(cur, kHartObj().g_ticks + tv->tv_sec*kernel::CLK_FREQ/kernel::INTERVAL + tv->tv_nsec*kernel::CLK_FREQ/(1000000*kernel::INTERVAL));
        for(int i = 0; i < kernel::NMAXSLEEP; ++i) {
            if(kHartObj().sleep_tasks[i].m_task == nullptr) {
                kHartObj().sleep_tasks[i] = tosleep;
                return sleep();
            }
        }
        return statcode::err;
    }
    void yield(){
        Log(debug,"yield!");
        auto &cur=kHartObj().curtask;
        sleepSave(cur->kctx.gpr);
    }
    xlen_t sysyield(){
        yield();
        return statcode::ok;
    }
    xlen_t kill() {
        auto &ctx = kHartObj().curtask->ctx;
        pid_t a_pid = ctx.x(10);
        int a_sig = ctx.x(11);

        if(a_pid == 0) { a_pid = kHartObj().curtask->getProcess()->pid(); } // FIXME: process group
        if(a_pid < -1) { a_pid = -a_pid; } // FIXME: process group
        if(a_pid > 0) {
            auto proc = (**kGlobObjs->procMgr)[a_pid];
            if(proc == nullptr) { statcode::err; }
            if(a_sig == 0) { statcode::ok; }
            unique_ptr<SignalInfo> tmp(nullptr);
            send(*proc, a_sig, tmp);
            return statcode::ok;
        }
        if(a_pid == -1) {
            if(a_sig == 0) { return statcode::ok; }
            bool success = false;
            auto procs = (**kGlobObjs->procMgr);
            int procsnum = procs.getObjNum();
            for (int i = 0; i < procsnum; ++i) {
                auto it = procs[i];
                if (it->pid() != 1) {
                    success = true;
                    unique_ptr<SignalInfo> tmp(nullptr);
                    send(*it, a_sig, tmp);
                }
            }
            return success ? statcode::ok : statcode::err;
        }
        return statcode::ok;
    }
    xlen_t tkill() {
        return kill();
    }
    xlen_t sigAction() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_sig = ctx.x(10);
        SignalAction *a_nact = (SignalAction*)ctx.x(11);
        SignalAction *a_oact = (SignalAction*)ctx.x(12);
        
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray nactarr(sizeof(SignalAction));
        SignalAction *nact = nullptr;
        if(a_nact != nullptr ) {
            nactarr = curproc->vmar.copyin((xlen_t)a_nact, nactarr.len);
            nact = (SignalAction*)nactarr.buff;
        }
        ByteArray oactarr(sizeof(SignalAction));
        SignalAction *oact = (SignalAction*)oactarr.buff;
        if(a_oact == nullptr) { oact = nullptr; }

        int ret = doSigAction(a_sig, nact, oact);
        if(ret==0 && a_oact!=nullptr) { curproc->vmar.copyout((xlen_t)a_oact, oactarr); }
        return ret;
    }
    xlen_t sigProcMask() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_how = ctx.x(10);
        SigSet *a_nset = (SigSet*)ctx.x(11);
        SigSet *a_oset = (SigSet*)ctx.x(12);
        size_t a_sigsetsize = ctx.x(13);

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray nsetarr = curproc->vmar.copyin((xlen_t)a_nset, sizeof(SigSet));
        SigSet *nset = (SigSet*)nsetarr.buff;
        ByteArray osetarr(sizeof(SigSet));
        SigSet *oset = (SigSet*)osetarr.buff;
        if(a_oset == nullptr) { oset = nullptr; }

        int ret = doSigProcMask(a_how, nset, oset, a_sigsetsize);
        if(ret==0 && oset!=nullptr) { curproc->vmar.copyout((xlen_t)a_oset, osetarr); }
        return ret;
    }
    xlen_t sigReturn() {
        return doSigReturn();
    }
    xlen_t times(void) {
        auto &ctx = kHartObj().curtask->ctx;
        proc::Tms *a_tms = (proc::Tms*)ctx.x(10);
        // if(a_tms == nullptr) { return statcode::err; } // a_tms留空时不管tms只返回ticks？

        auto curproc = kHartObj().curtask->getProcess();
        if(a_tms != nullptr) { curproc->vmar.copyout((xlen_t)a_tms, ByteArray((uint8*)&curproc->ti, sizeof(proc::Tms))); }
        // acquire(&tickslock);
        int ticks = (int)(kHartObj().g_ticks/kernel::INTERVAL);
        // release(&tickslock);

        return ticks;
    }
    xlen_t setGid() {
        auto &ctx = kHartObj().curtask->ctx;
        gid_t a_gid = ctx.x(10);
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->rgid() = curproc->egid() = curproc->sgid() = a_gid;

        return statcode::ok;
    }
    xlen_t setUid() {
        auto &ctx = kHartObj().curtask->ctx;
        uid_t a_uid = ctx.x(10);
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->ruid() = curproc->euid() = curproc->suid() = a_uid;

        return statcode::ok;
    }
    xlen_t getRESgid() {
        auto &ctx = kHartObj().curtask->ctx;
        gid_t *a_rgid = (gid_t*)ctx.x(10);
        gid_t *a_egid = (gid_t*)ctx.x(11);
        gid_t *a_sgid = (gid_t*)ctx.x(12);
        if(a_rgid==nullptr || a_egid==nullptr || a_sgid==nullptr) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar.copyout((xlen_t)a_rgid, ByteArray((uint8*)&curproc->rgid(), sizeof(gid_t)));
        curproc->vmar.copyout((xlen_t)a_egid, ByteArray((uint8*)&curproc->egid(), sizeof(gid_t)));
        curproc->vmar.copyout((xlen_t)a_sgid, ByteArray((uint8*)&curproc->sgid(), sizeof(gid_t)));

        return statcode::ok;
    }
    xlen_t getRESuid() {
        auto &ctx = kHartObj().curtask->ctx;
        uid_t *a_ruid = (uid_t*)ctx.x(10);
        uid_t *a_euid = (uid_t*)ctx.x(11);
        uid_t *a_suid = (uid_t*)ctx.x(12);
        if(a_ruid==nullptr || a_euid==nullptr || a_suid==nullptr) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar.copyout((xlen_t)a_ruid, ByteArray((uint8*)&curproc->ruid(), sizeof(uid_t)));
        curproc->vmar.copyout((xlen_t)a_euid, ByteArray((uint8*)&curproc->euid(), sizeof(uid_t)));
        curproc->vmar.copyout((xlen_t)a_suid, ByteArray((uint8*)&curproc->suid(), sizeof(uid_t)));

        return statcode::ok;
    }
    xlen_t setPGid() {
        auto &ctx = kHartObj().curtask->ctx;
        pid_t a_pid = (pid_t)ctx.x(10);
        pid_t a_pgid = (pid_t)ctx.x(11);

        auto proc = (a_pid==0 ? kHartObj().curtask->getProcess() : (**kGlobObjs->procMgr)[a_pid]);
        if(proc == nullptr) { return -ESRCH; }
        proc->pgid() = (a_pgid==0 ? proc->pid() : a_pgid);

        return statcode::ok;
    }
    xlen_t getPGid() {
        auto &ctx = kHartObj().curtask->ctx;
        pid_t a_pid = (pid_t)ctx.x(10);
        
        auto proc = (a_pid==0 ? kHartObj().curtask->getProcess() : (**kGlobObjs->procMgr)[a_pid]);
        if(proc == nullptr) { return -ESRCH; }

        return proc->pgid();
    }
    xlen_t getSid() {
        auto &ctx = kHartObj().curtask->ctx;
        pid_t a_pid = (pid_t)ctx.x(10);
        
        auto proc = (a_pid==0 ? kHartObj().curtask->getProcess() : (**kGlobObjs->procMgr)[a_pid]);
        if(proc == nullptr) { return -ESRCH; }

        return proc->sid();
    }
    xlen_t setSid() {
        auto &ctx = kHartObj().curtask->ctx;

        auto curproc = kHartObj().curtask->getProcess();
        if(curproc->pgid() == curproc->pid()) { return -EPERM; }
        curproc->sid()  = curproc->pid();
        curproc->pgid() = curproc->pid();

        return curproc->sid();
    }
    xlen_t getGroups() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_size = ctx.x(10);
        gid_t *a_list = (gid_t*)ctx.x(11);
        if(a_list == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        if(a_size == 0) { return curproc->getGroupsNum(); }
        ByteArray grps = curproc->getGroups(a_size);
        curproc->vmar.copyout((xlen_t)a_list, grps);

        return grps.len / sizeof(gid_t);
    }
    xlen_t setGroups() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_size = ctx.x(10);
        gid_t *a_list = (gid_t*)ctx.x(11);
        if(a_list == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray listarr = curproc->vmar.copyin((xlen_t)a_list, a_size * sizeof(gid_t));
        ArrayBuff<gid_t> grps((gid_t*)listarr.buff, a_size);
        curproc->setGroups(grps);

        return 0;
    }
    xlen_t uName(void) {
        auto &ctx = kHartObj().curtask->ctx;
        struct UtSName *a_uts = (struct UtSName*)ctx.x(10);
        if(a_uts == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        static struct UtSName uts = { "domainname", "machine", "nodename", "release", "sysname", "version" };
        curproc->vmar.copyout((xlen_t)a_uts, ByteArray((uint8*)&uts, sizeof(uts)));

        return statcode::ok;
    }
    xlen_t uMask() {
        auto &ctx = kHartObj().curtask->ctx;
        mode_t a_mask = ctx.x(10);

        auto curproc = kHartObj().curtask->getProcess();

        return curproc->setUMask(a_mask);
    }
    xlen_t getRLimit() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_rsrc = ctx.x(10);
        RLim *a_rlim = (RLim*)ctx.x(11);
        if(a_rlim==nullptr || a_rsrc<0 || a_rsrc>=RSrc::RLIMIT_NLIMITS) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar.copyout((xlen_t)a_rlim, curproc->getRLimit(a_rsrc));

        return 0;
    }
    xlen_t setRLimit() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_rsrc = ctx.x(10);
        const RLim *a_rlim = (const RLim*)ctx.x(11);
        if(a_rlim==nullptr || a_rsrc<0 || a_rsrc>=RSrc::RLIMIT_NLIMITS) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray rlimarr = curproc->vmar.copyin((xlen_t)a_rlim, sizeof(RLim));
        const RLim *rlim = (const RLim*)rlimarr.buff;
        if(rlim->rlim_cur > rlim->rlim_max) { return statcode::err; }
        
        return curproc->setRLimit(a_rsrc, rlim);
    }
    xlen_t getTimeOfDay() {
        auto &ctx = kHartObj().curtask->ctx;
        auto a_ts = ctx.x(10);
        if(a_ts == 0) { return -EINVAL; }
    
        auto curproc = kHartObj().curtask->getProcess();
        auto cur=eastl::chrono::system_clock::now();
        auto ticks=cur.time_since_epoch().count();
        struct timespec ts = { ticks/kernel::CLK_FREQ, ((100000*ticks/kernel::CLK_FREQ)%100000)*10 };
        curproc->vmar[a_ts]<<ts;

        return statcode::ok;
    }
    xlen_t getPid(){
        return kHartObj().curtask->getProcess()->pid();
    }
    xlen_t getPPid() { return kHartObj().curtask->getProcess()->parentProc()->pid(); }
    int sleep(){
        auto &cur=kHartObj().curtask;
        cur->sleep();
        yield();
        return statcode::ok;
    }
    xlen_t getUid() {
        return kHartObj().curtask->getProcess()->ruid();
    }
    xlen_t getEuid() {
        return kHartObj().curtask->getProcess()->euid();
    }
    xlen_t getGid() {
        return kHartObj().curtask->getProcess()->rgid();
    }
    xlen_t getEgid() {
        return kHartObj().curtask->getProcess()->egid();
    }
    xlen_t getTid() {
        return kHartObj().curtask->tid();
    }
    xlen_t clone(){
        auto &ctx=kHartObj().curtask->ctx;
        xlen_t func=ctx.x(10),childStack=ctx.x(11);
        int flags=ctx.x(12);
        auto pid=proc::clone(kHartObj().curtask);
        auto thrd=(**kGlobObjs->procMgr)[pid]->defaultTask();
        if(childStack)thrd->ctx.sp()=childStack;
        // if(func)thrd->ctx.pc=func;
        Log(debug,"clone curproc=%d, new proc=%d",kHartObj().curtask->getProcess()->pid(),pid);
        return pid;
    }
    int waitpid(pid_t pid,xlen_t wstatus,int options){
        Log(debug,"waitpid(pid=%d,options=%d)",pid,options);
        auto curproc=kHartObj().curtask->getProcess();
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
        if(wstatus)curproc->vmar[wstatus]<<(int)(target->exitstatus<<8);
        target->zombieExit();
        return rt;
    }
    xlen_t wait(){
        auto &cur=kHartObj().curtask;
        auto &ctx=cur->ctx;
        pid_t pid=ctx.x(10);
        xlen_t wstatus=ctx.x(11);
        return waitpid(pid,wstatus,0);
    }
    xlen_t syncFS() {
        // 同sync()
        return statcode::ok;
    }
    xlen_t brk(){
        auto &ctx=kHartObj().curtask->ctx;
        auto &curproc=*kHartObj().curtask->getProcess();
        xlen_t addr=ctx.x(10);
        return curproc.brk(addr);
    }
    xlen_t mmap(xlen_t addr,size_t len,int prot,int flags,int fd,int offset){
        auto &curproc=*kHartObj().curtask->getProcess();
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
        if(!pages)return -1;
        // assert(pages);
        Arc<vm::VMO> vmo;
        /// @todo register for shared mapping
        /// @todo copy content to vmo
        if(fd!=-1){
            auto file=curproc.ofile(fd);
            vmo=file->vmo();
        } else {
            auto pager=make_shared<SwapPager>(nullptr);
            pager->backingregion={0x0,pn2addr(pages)};
            vmo=make_shared<VMOPaged>(pages,pager);
        }
        // actual map
        /// @todo fix flags
        auto mappingType= fd==-1 ?PageMapping::MappingType::file : PageMapping::MappingType::anon;
        auto sharingType=(PageMapping::SharingType)(flags>>8);
        curproc.vmar.map(PageMapping{vpn,pages,0,vmo,PageMapping::prot2perm((PageMapping::Prot)prot),mappingType,sharingType});
        // return val
        return pn2addr(vpn);
    }
    xlen_t munmap(xlen_t addr,size_t len){
        auto &ctx=kHartObj().curtask->ctx;
        auto &curproc=*kHartObj().curtask->getProcess();
        /// @todo len, partial unmap?
        using namespace vm;
        if(addr&vaddrOffsetMask){
            Log(warning,"munmap addr not aligned!");
            return statcode::err;
        }
        auto region=vm::Segment{addr2pn(addr),addr2pn(addr+len)};
        curproc.vmar.unmap(region);
    }
    sysrt_t mprotect(xlen_t addr,size_t len,int prot){
        return 0;
    }
    int execve_(shared_ptr<fs::File> file,vector<klib::ByteArray> &args,char **envp){
        auto &ctx=kHartObj().curtask->ctx;
        /// @todo reset cur proc vmar, refer to man 2 execve for details
        auto curproc=kHartObj().curtask->getProcess();
        curproc->vmar.reset();
        /// @todo destroy other threads
        /// @todo reset curtask cpu context
        ctx=proc::Context();
        // static_cast<proc::Context>(kHartObj().curtask->kctx)=proc::Context();
        ctx.sp()=proc::UserStackDefault;
        /// load elf
        auto [pc,brk]=ld::loadElf(file,curproc->vmar);
        ctx.pc=pc;
        curproc->heapTop=curproc->heapBottom=brk;
        /// setup stack
        ArrayBuff<xlen_t> argv(args.size()+1);
        int argc=0;
        auto ustream=curproc->vmar[ctx.sp()];
        ustream.reverse=true;
        xlen_t endMarker=0x114514;
        ustream<<endMarker;
        for(auto arg:args){
            ustream<<arg;
            argv.buff[argc++]=ustream.addr();
        }
        // ctx.sp()=ustream.addr();
        argv.buff[argc]=0;
        // ctx.sp()-=argv.len*sizeof(xlen_t);
        // ctx.sp()-=ctx.sp()%16;
        vector<Elf64_auxv_t> auxv;
        auxv.push_back({AT_PAGESZ,vm::pageSize});
        auxv.push_back({AT_NULL,0});
        ustream<<ArrayBuff(auxv.data(),auxv.size());
        ustream<<nullptr;
        ustream<<argv;
        auto argvsp=ustream.addr();
        // assert(argvsp==ctx.sp());
        /// @bug alignment?
        // ctx.sp()-=8;
        ustream<<(xlen_t)argc;
        auto argcsp=ustream.addr();
        ctx.sp()=ustream.addr();
        // assert(argcsp==ctx.sp());
        Log(debug,"$sp=%x, argc@%x, argv@%x",ustream.addr(),argcsp,argvsp);
        /// setup argc, argv
        return ctx.sp();
    }
    xlen_t execve(xlen_t pathuva,xlen_t argv,xlen_t envp){
        auto &cur=kHartObj().curtask;
        auto &ctx=cur->ctx;
        auto curproc=cur->getProcess();

        /// @brief get executable from path
        ByteArray pathbuf = curproc->vmar.copyinstr(pathuva, FAT32_MAX_PATH);
        // string path((char*)pathbuf.buff,pathbuf.len);
        char *path=(char*)pathbuf.buff;

        Log(debug,"execve(path=%s,)",path);
        // auto Ent=fs::entEnter(path);
        shared_ptr<DEntry> Ent=Path(path).pathSearch();
        auto file=make_shared<File>(Ent,fs::FileOp::read);
        // auto buf=file->read(Ent->getINode()->rFileSize());
        // auto buf=klib::ByteArray{0};
        // buf.buff=(uint8_t*)((xlen_t)&_uimg_start);buf.len=0x3ba0000;

        /// @brief get args
        vector<ByteArray> args;
        xlen_t str;
        do{
            curproc->vmar[argv]>>str;
            if(!str)break;
            auto buff=curproc->vmar.copyinstr(str,100);
            args.push_back(buff);
            argv+=sizeof(char*);
        }while(str!=0);
        /// @brief get envs
        return execve_(file,args,0);
    }
    expected<xlen_t,string> reboot(){
        auto &ctx=kHartObj().curtask->ctx;
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
    xlen_t setTidAddress(){
        auto &cur=kHartObj().curtask;
        auto &ctx=cur->ctx;
        xlen_t tidptr=ctx.a0();
        // cur.attrs.clearChildTid=tidptr;
        return cur->id;
    }
const char *syscallHelper[sys::syscalls::nSyscalls];
#define DECLSYSCALL(x,ptr) syscallPtrs[x]=reinterpret_cast<syscall_t>(ptr);syscallHelper[x]=#x;
    void init(){
        using scnum=sys::syscalls;
        for(int i=0;i<scnum::nSyscalls;i++)syscallHelper[i]="";
        DECLSYSCALL(scnum::none,none);
        DECLSYSCALL(scnum::testexit,testExit);
        DECLSYSCALL(scnum::testyield,sysyield);
        DECLSYSCALL(scnum::testwrite,write);
        DECLSYSCALL(scnum::testbio,testBio);
        DECLSYSCALL(scnum::testidle,testIdle);
        DECLSYSCALL(scnum::testmount,testMount);
        DECLSYSCALL(scnum::testfatinit,testFATInit);
        DECLSYSCALL(scnum::reboot,reboot1);
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
        DECLSYSCALL(scnum::getcwd,getCwd);
        DECLSYSCALL(scnum::dup,dup);
        DECLSYSCALL(scnum::dup3,dup3);
        DECLSYSCALL(scnum::fcntl,fCntl);
        DECLSYSCALL(scnum::mkdirat,mkDirAt);
        DECLSYSCALL(scnum::linkat,linkAt);
        DECLSYSCALL(scnum::unlinkat,unlinkAt);
        DECLSYSCALL(scnum::symlinkat,symLinkAt);
        DECLSYSCALL(scnum::umount2,umount2);
        DECLSYSCALL(scnum::mount,mount);
        DECLSYSCALL(scnum::statfs,statFS);
        DECLSYSCALL(scnum::faccessat,fAccessAt);
        DECLSYSCALL(scnum::chdir,chDir);
        DECLSYSCALL(scnum::fchdir,fChDir);
        DECLSYSCALL(scnum::fchmod,fChMod);
        DECLSYSCALL(scnum::fchmodat,fChModAt);
        DECLSYSCALL(scnum::fchownat,fChOwnAt);
        DECLSYSCALL(scnum::fchown,fChOwn);
        DECLSYSCALL(scnum::openat,openAt);
        DECLSYSCALL(scnum::close,close);
        DECLSYSCALL(scnum::pipe2,pipe2);
        DECLSYSCALL(scnum::getdents64,getDents64);
        DECLSYSCALL(scnum::lseek,lSeek);
        DECLSYSCALL(scnum::read,read);
        DECLSYSCALL(scnum::write,write);
        DECLSYSCALL(scnum::readv,readv);
        DECLSYSCALL(scnum::writev,writev);
        DECLSYSCALL(scnum::sendfile,sendFile);
        DECLSYSCALL(scnum::readlinkat,readLinkAt);
        DECLSYSCALL(scnum::fstatat,fStatAt);
        DECLSYSCALL(scnum::fstat,fStat);
        DECLSYSCALL(scnum::sync,sync);
        DECLSYSCALL(scnum::exit,exit);
        DECLSYSCALL(scnum::exit_group,exitGroup);
        DECLSYSCALL(scnum::settidaddress,setTidAddress)
        DECLSYSCALL(scnum::nanosleep,nanoSleep);
        DECLSYSCALL(scnum::yield,sysyield);
        DECLSYSCALL(scnum::kill,kill);
        DECLSYSCALL(scnum::tkill,tkill);
        DECLSYSCALL(scnum::sigaction,sigAction);
        DECLSYSCALL(scnum::sigprocmask,sigProcMask);
        DECLSYSCALL(scnum::sigreturn,sigReturn);
        DECLSYSCALL(scnum::setgid,setGid);
        DECLSYSCALL(scnum::setuid,setUid);
        DECLSYSCALL(scnum::getresgid,getRESgid);
        DECLSYSCALL(scnum::getresuid,getRESuid);
        DECLSYSCALL(scnum::setpgid,setPGid);
        DECLSYSCALL(scnum::getpgid,getPGid);
        DECLSYSCALL(scnum::getsid,getSid);
        DECLSYSCALL(scnum::setsid,setSid);
        DECLSYSCALL(scnum::getgroups,getGroups);
        DECLSYSCALL(scnum::setgroups,setGroups);
        DECLSYSCALL(scnum::times,times);
        DECLSYSCALL(scnum::uname,uName);
        DECLSYSCALL(scnum::getrlimit,getRLimit);
        DECLSYSCALL(scnum::setrlimit,setRLimit);
        DECLSYSCALL(scnum::umask,uMask);
        DECLSYSCALL(scnum::gettimeofday,getTimeOfDay);
        DECLSYSCALL(scnum::getpid,getPid);
        DECLSYSCALL(scnum::getppid,getPPid);
        DECLSYSCALL(scnum::getuid,getUid);
        DECLSYSCALL(scnum::geteuid,getEuid);
        DECLSYSCALL(scnum::getgid,getGid);
        DECLSYSCALL(scnum::getegid,getEgid);
        DECLSYSCALL(scnum::gettid,getTid);
        DECLSYSCALL(scnum::brk,brk);
        DECLSYSCALL(scnum::munmap,munmap);
        DECLSYSCALL(scnum::clone,clone);
        DECLSYSCALL(scnum::execve,execve);
        DECLSYSCALL(scnum::mmap,mmap);
        DECLSYSCALL(scnum::wait,wait);
        DECLSYSCALL(scnum::syncfs,syncFS);
    }
} // namespace syscall
