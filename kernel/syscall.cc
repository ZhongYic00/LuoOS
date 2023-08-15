#include "kernel.hh"
#include "sched.hh"
#include "fs.hh"
#include "ld.hh"
#include "sbi.hh"
#include "bio.hh"
#include "vm/vmo.hh"
#include "thirdparty/expected.hpp"
#include <EASTL/chrono.h>
#include <linux/reboot.h>
#include <linux/unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <asm/poll.h>
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
    using signal::SigAct;
    using signal::SigSet;
    using signal::SigInfo;
    using signal::sigSend;
    // using signal::sigAction;
    // using signal::sigProcMask;
    // using signal::sigReturn;
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
    extern sysrt_t testFATInit();
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
    xlen_t ioCtl() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_fd = ctx.x(10);
        xlen_t a_request = ctx.x(11);
        addr_t a_arg = ctx.x(12); // @todo 还没用上
        
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        auto rt=file->ioctl(a_request,a_arg);
        return rt;
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
        if(!S_ISDIR(ep->getINode()->rMode())){ return statcode::err; }

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
        if(!S_ISDIR(nwd->obj.getEntry()->getINode()->rMode())) { return -ENOTDIR; }
        curproc->cwd = nwd->obj.getEntry();
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
        return statcode::ok;
    }
    xlen_t getDents64(void) {
        auto &ctx = kHartObj().curtask->ctx;
        int a_dirfd = ctx.x(10);
        DStat *a_buf = (DStat*)ctx.x(11);
        size_t a_len = ctx.x(12);
        if(a_buf==nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> dir = curproc->ofile(a_dirfd);
        if(dir == nullptr) { return -EBADF; }
        if(dir->obj.rType() != fs::FileType::entry) { return -EINVAL; }
        ArrayBuff<DStat> buf(a_len / sizeof(DStat));
        int len = dir->readDir(buf);
        if(len > 0) { curproc->vmar.copyout((xlen_t)a_buf, ByteArray((uint8*)buf.buff, len)); }
        return len;
    }
    xlen_t lSeek(int fd,off_t offset,int whence) {
        auto &ctx = kHartObj().curtask->ctx;

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(fd);
        if(!file) { return -EBADF; }

        return file->lSeek(offset, whence);
    }
    xlen_t read(){
        auto &ctx=kHartObj().curtask->ctx;
        int fd = ctx.x(10);
        xlen_t uva = ctx.x(11), len = ctx.x(12);
        auto file = kHartObj().curtask->getProcess()->ofile(fd);
        if(file == nullptr) { return -EBADF; }
        auto buf_=new uint8_t[len];
        ByteArray buf(buf_,len);
        auto rdbytes=file->read(buf);
        kHartObj().curtask->getProcess()->vmar[uva] << ByteArray(buf_,rdbytes);
        delete[] buf_;
        return rdbytes;
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
    void exit(){
        auto cur=kHartObj().curtask;
        auto status=cur->ctx.a0();
        cur->getProcess()->exit(status);
        yield();
    }
    void exitGroup() {
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
        ssize_t ret = statcode::err;
        if(a_offset != nullptr) {
            ByteArray offsetarr = curproc->vmar.copyin((xlen_t)a_offset, sizeof(off_t));
            offset = (off_t*)offsetarr.buff;
            ret = infile->sendFile(outfile, offset, a_len);
            curproc->vmar.copyout((xlen_t)a_offset, offsetarr);
        }
        else { ret = infile->sendFile(outfile, offset, a_len); }

        return ret;
    }
    xlen_t pPoll() {
        auto &ctx=kHartObj().curtask->ctx;
        pollfd *a_fds = (pollfd*)ctx.x(10);
        xlen_t a_nfds = ctx.x(11);
        timespec *a_tsp = (timespec*)ctx.x(12);
        SigSet *a_sigmask = (SigSet*)ctx.x(13);
        size_t a_sigsetsize = ctx.x(14);
        if(a_fds==nullptr || a_nfds<0 || a_sigsetsize!=sizeof(SigSet)) { return -EINVAL; }
        
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray fdsarr = curproc->vmar.copyin((xlen_t)a_fds, a_nfds*sizeof(pollfd));
        pollfd *fds = (pollfd*)fdsarr.buff;
        // @todo: 处理信号阻塞
        // @todo: 处理等待时间（类似nanoSleep）
        int ret = 0;
        for (xlen_t i = 0; i < a_nfds; ++i) {  // @todo: 需要完善
            fds[i].revents = 0;
            if (fds[i].fd < 0) { continue; }
            if (fds[i].events & POLLIN) { fds[i].revents |= POLLIN; }  // @todo: we assume this is always available
            if (fds[i].events & POLLOUT) { fds[i].revents |= POLLOUT; }  // @todo: we assume this is always available
            ++ret;
        }
        curproc->vmar.copyout((xlen_t)a_fds, fdsarr);

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
        if(entry == nullptr) { return -ENOENT; }
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
        curproc->vmar.copyout((xlen_t)a_kst, ByteArray((uint8*)&file->obj.kst(), sizeof(KStat)));
        // @bug 用户态读到的数据混乱
        return statcode::ok;
    }
    xlen_t sync() {
        // 现阶段实现为write-through缓存，自动同步
        return statcode::ok;
    }
    int futex(int *uaddr, int futex_op, int val,
                 const struct timespec *timeout,   /* or: uint32_t val2 */
                 int *uaddr2, int val3);
    __attribute__((naked))
    void sleepSave(ptr_t gpr){
        saveContextTo(gpr);
        auto curtask=kHartObj().curtask;
        curtask->kctx.pc=curtask->kctx.ra();
        curtask->kctxs.push(curtask->kctx);
        schedule();
        _strapexit(); //TODO check
    }
    extern sysrt_t nanoSleep(const struct timespec *req, struct timespec *rem);
    extern int clock_gettime (clockid_t __clock_id, struct timespec *__tp);
    void yield(){
        Log(debug,"yield!");
        auto &cur=kHartObj().curtask;
        sleepSave(cur->kctx.gpr);
    }
    xlen_t sysyield(){
        yield();
        return statcode::ok;
    }
    extern sysrt_t kill(pid_t pid, int sig);
    extern sysrt_t tkill(int tid, int sig);
    xlen_t sigAction() {
        auto &ctx = kHartObj().curtask->ctx;
        int a_sig = ctx.x(10);
        SigAct *a_nact = (SigAct*)ctx.x(11);
        SigAct *a_oact = (SigAct*)ctx.x(12);
        
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray oactarr(sizeof(SigAct));
        SigAct *oact = (SigAct*)oactarr.buff;
        if(a_oact == nullptr) { oact = nullptr; }
        SigAct *nact = nullptr;
        int ret = statcode::err;
        if(a_nact != nullptr ) {
            ByteArray nactarr = curproc->vmar.copyin((xlen_t)a_nact, sizeof(SigAct));
            nact = (SigAct*)nactarr.buff;
            ret = signal::sigAction(a_sig, nact, oact);
        }
        else { ret = signal::sigAction(a_sig, nact, oact); }
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
        if(!a_nset) return 0;
        SigSet nset,oset;
        curproc->vmar[(addr_t)a_nset]>> nset;
        int ret = signal::sigProcMask(a_how, &nset, &oset, a_sigsetsize);
        if(a_oset) curproc->vmar[(addr_t)a_oset]<<oset;
        return ret;
    }
    xlen_t sigReturn() {
        // should never be called
        return signal::sigReturn();
    }
    extern sysrt_t times(struct ::tms *buf);
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
        addr_t a_uts=ctx.x(10);
        if(!a_uts) { return -EFAULT; }
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar[a_uts]<<kInfo.uts;
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
    extern sysrt_t getrusage(int who, struct ::rusage *usage);
    extern int gettimeofday (struct timeval *__restrict __tv,
			 struct timezone *__tz) __THROW __nonnull ((1));
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
    extern long clone(unsigned long flags, void *stack,
                      int *parent_tid, unsigned long tls,
                      int *child_tid);
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
        {   // update curproc stats
            /// @note should be here?
            auto &stats=curproc->stats,&cstats=target->stats;
            stats.ti.tms_cstime+=cstats.ti.tms_stime+cstats.ti.tms_cstime;
            stats.ti.tms_cutime+=cstats.ti.tms_utime+cstats.ti.tms_cutime;
        }
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
    sysrt_t mmap(addr_t addr,size_t len,int prot,int flags,int fd,int offset);
    sysrt_t munmap(addr_t addr,size_t len);
    sysrt_t mremap(addr_t oldaddr, size_t oldsize,size_t newsize, int flags, addr_t newaddr);
    sysrt_t mprotect(addr_t addr,size_t len,int prot);
    sysrt_t madvise(addr_t addr,size_t length,int advice);
    sysrt_t mlock(addr_t addr, size_t len);
    sysrt_t munlock(addr_t addr, size_t len);
    int execve_(shared_ptr<fs::File> file,vector<klib::ByteArray> &args,vector<klib::ByteArray> &envs){
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
        auto [entry,brk,info]=ld::loadElf(file,curproc->vmar);
        ctx.pc=entry;
        curproc->heapTop=curproc->heapBottom=brk;
        /// setup stack
        ArrayBuff<xlen_t> argv(args.size()+1);
        vector<addr_t> envps;
        int argc=0;
        auto ustream=curproc->vmar[ctx.sp()];
        ustream.reverse=true;
        xlen_t endMarker=0x114514;
        ustream<<endMarker;
        ustream<<0ul;
        auto dlRandomAddr=ustream.addr();
        for(auto arg:args){
            ustream<<arg;
            argv.buff[argc++]=ustream.addr();
        }
        for(auto env:envs){
            ustream<<env;
            envps.push_back(ustream.addr());
        }
        // ctx.sp()=ustream.addr();
        argv.buff[argc]=0;
        // ctx.sp()-=argv.len*sizeof(xlen_t);
        // ctx.sp()-=ctx.sp()%16;
        vector<Elf64_auxv_t> auxv;
        /* If the main program was already loaded by the kernel,
        * AT_PHDR will point to some location other than the dynamic
        * linker's program headers. */
        if(info.phdr){
            auxv.push_back({AT_PHDR,info.phdr});
            auxv.push_back({AT_PHENT,info.e_phentsize});
            auxv.push_back({AT_PHNUM,info.e_phnum});
        }
        auxv.push_back({AT_PAGESZ,vm::pageSize});
        auxv.push_back({AT_BASE,proc::interpreterBase});
        auxv.push_back({AT_ENTRY,info.e_entry});
        auxv.push_back({AT_RANDOM,dlRandomAddr});
        auxv.push_back({AT_NULL,0});
        ustream<<ArrayBuff(auxv.data(),auxv.size());
        ustream<<nullptr;
        ustream<<envps;
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
        curproc->exe=Path(path).pathAbsolute();

        Log(debug,"execve(path=%s,)",path);
        shared_ptr<DEntry> Ent=Path(path).pathSearch();
        auto file=make_shared<File>(Ent,fs::FileOp::read);
        
        vector<ByteArray> args,envs;

        // check whether elf or script
        auto interprtArg="sh\0";
        if(!ld::isElf(file)){
            string interpreter="/busybox";
            file=make_shared<File>(Path(interpreter).pathSearch(),fs::FileOp::read);
            args.push_back(ByteArray((uint8_t*)interprtArg,strlen(interprtArg)+1));
        }

        /// @brief get args
        xlen_t str;
        do{
            curproc->vmar[argv]>>str;
            if(!str)break;
            auto buff=curproc->vmar.copyinstr(str,200);
            args.push_back(buff);
            argv+=sizeof(char*);
        }while(str!=0);
        /// @brief get envs
        auto ustream=curproc->vmar[envp];
        if(envp) do{
            ustream>>str;
            if(!str)break;
            auto buff=curproc->vmar.copyinstr(str,100);
            envs.push_back(buff);
        }while(str!=0);
        return execve_(file,args,envs);
    }
    expected<xlen_t,string> reboot(){
        auto &ctx=kHartObj().curtask->ctx;
        int magic=ctx.x(10),magic2=ctx.x(11),cmd=ctx.x(12);
        if(!(magic==LINUX_REBOOT_MAGIC1 && magic2==LINUX_REBOOT_MAGIC2))return nonstd::make_unexpected("magic num unmatched!");
        if(cmd==LINUX_REBOOT_CMD_POWER_OFF){
            Log(error,"LuoOS Shutdown! Bye-Bye");
            sbi_shutdown();
        }
        return statcode::err;
    }
    xlen_t reboot1(){
        if(auto ret=reboot()){
            return ret.value();
        } else {
            Log(warning,"syscall failed, reason: %s",ret.error().c_str());
            return statcode::err;
        }
    }
    extern sysrt_t setTidAddress(int *tidptr);
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
        DECLSYSCALL(scnum::ioctl,ioCtl);
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
        DECLSYSCALL(scnum::ppoll,pPoll);
        DECLSYSCALL(scnum::readlinkat,readLinkAt);
        DECLSYSCALL(scnum::fstatat,fStatAt);
        DECLSYSCALL(scnum::fstat,fStat);
        DECLSYSCALL(scnum::sync,sync);
        DECLSYSCALL(scnum::exit,exit);
        DECLSYSCALL(scnum::exit_group,exitGroup);
        DECLSYSCALL(scnum::settidaddress,setTidAddress)
        DECLSYSCALL(scnum::futex,futex);
        DECLSYSCALL(scnum::nanosleep,nanoSleep);
        DECLSYSCALL(scnum::clock_gettime,clock_gettime);
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
        DECLSYSCALL(scnum::getrusage,getrusage);
        DECLSYSCALL(scnum::umask,uMask);
        DECLSYSCALL(scnum::gettimeofday,gettimeofday);
        DECLSYSCALL(scnum::getpid,getPid);
        DECLSYSCALL(scnum::getppid,getPPid);
        DECLSYSCALL(scnum::getuid,getUid);
        DECLSYSCALL(scnum::geteuid,getEuid);
        DECLSYSCALL(scnum::getgid,getGid);
        DECLSYSCALL(scnum::getegid,getEgid);
        DECLSYSCALL(scnum::gettid,getTid);
        DECLSYSCALL(scnum::brk,brk);
        DECLSYSCALL(scnum::munmap,munmap);
        DECLSYSCALL(scnum::mremap,mremap);
        DECLSYSCALL(scnum::clone,clone);
        DECLSYSCALL(scnum::execve,execve);
        DECLSYSCALL(scnum::mmap,mmap);
        DECLSYSCALL(scnum::mprotect,mprotect);
        DECLSYSCALL(scnum::mlock,mlock);
        DECLSYSCALL(scnum::munlock,munlock);
        DECLSYSCALL(scnum::madvise,madvise);
        DECLSYSCALL(scnum::wait,wait);
        DECLSYSCALL(scnum::syncfs,syncFS);
    }
} // namespace syscall
