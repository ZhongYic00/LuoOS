#include "kernel.hh"
#include "proc.hh"
#include <linux/sched.h>
#include <linux/unistd.h>
#include <sys/resource.h>

namespace syscall
{
    using sys::statcode;
    using namespace fs;
    using proc::FdCwd;
    sysrt_t getCwd(char *a_buf,size_t a_len) {
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
    sysrt_t dup(int a_fd) {
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return curproc->fdAlloc(file);
    }
    sysrt_t dup3(int a_fd,int a_newfd,int a_flags) {
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
    sysrt_t fCntl(int a_fd,int a_cmd,uint64_t a_arg) {
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
    sysrt_t ioCtl(int a_fd,uint64_t a_request,addr_t a_arg) {
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        auto rt=file->ioctl(a_request,a_arg);
        return rt;
    }
    sysrt_t mkDirAt(int a_basefd,const char *a_path,mode_t a_mode) {
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
    sysrt_t unlinkAt(int a_basefd,const char *a_path,int a_flags) {
        if(a_path == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        return Path(path, base).pathHardUnlink();
    }
    sysrt_t symLinkAt(const char *a_target,int a_basefd,const char *a_linkpath) {
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray targetarr = curproc->vmar.copyinstr((xlen_t)a_target, FAT32_MAX_PATH);
        ByteArray linkarr = curproc->vmar.copyinstr((xlen_t)a_linkpath, FAT32_MAX_PATH);
        const char *target = (const char*)targetarr.buff;
        const char *link = (const char*)linkarr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        return Path(link, base).pathSymLink(target);
    }
    sysrt_t linkAt(int a_oldbasefd,const char *a_oldpath,int a_newbasefd,const char *a_newpath,int a_flags) {
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
    sysrt_t umount2(const char *a_devpath,int a_flags) {
        if(a_devpath == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray devpatharr = curproc->vmar.copyinstr((xlen_t)a_devpath, FAT32_MAX_PATH);
        char *devpath = (char*)devpatharr.buff;
        return Path(devpath).pathUnmount();
    }
    sysrt_t mount(const char *a_devpath,const char *a_mountpath, const char *a_fstype, uint64_t flags, const void *a_data) {
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
    sysrt_t statFS(const char *a_path,StatFS *a_buf) {
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        const char *path = (const char*)patharr.buff;

        shared_ptr<DEntry> entry = Path(path).pathSearch();
        if(entry == nullptr) { return statcode::err; }
        StatFS stat = *(entry->getINode()->getSpBlk()->getFS());
        curproc->vmar.copyout((xlen_t)a_buf, ByteArray((uint8*)&stat, sizeof(stat)));

        return statcode::ok;
    }
    sysrt_t fAccessAt(int a_basefd,const char *a_path,int a_mode,int a_flags) {
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
    sysrt_t chDir(const char *a_path) {
        if(a_path == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        shared_ptr<DEntry> ep = Path(path).pathSearch();
        if(ep == nullptr) { return statcode::err; }
        if(!S_ISDIR(ep->getINode()->rMode())){ return statcode::err; }

        curproc->cwd = ep;
        curproc->files[FdCwd] = make_shared<File>(curproc->cwd, O_RDWR);

        return statcode::ok;
    }
    sysrt_t fChDir(int a_fd) {
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> nwd = curproc->ofile(a_fd);
        if(nwd == nullptr) { return -EBADF; }
        if(!S_ISDIR(nwd->obj.getEntry()->getINode()->rMode())) { return -ENOTDIR; }
        curproc->cwd = nwd->obj.getEntry();
        curproc->files[FdCwd] = nwd;

        return statcode::ok;
    }
    sysrt_t fChMod(int a_fd,mode_t a_mode) {
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return file->chMod(a_mode);
    }
    sysrt_t fChModAt(int a_basefd,const char *a_path,mode_t a_mode,int a_flags) {
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
    sysrt_t fChOwnAt(int a_basefd,const char *a_path,uid_t a_uid,gid_t a_gid,int a_flags) {
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
    sysrt_t fChOwn(int a_fd,uid_t a_uid,gid_t a_gid) {
        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return file->chOwn(a_uid, a_gid);
    }
    sysrt_t openAt(int a_basefd,const char *a_path,int a_flags,mode_t a_mode) {
        if(a_path==nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray patharr = curproc->vmar.copyinstr((xlen_t)a_path, FAT32_MAX_PATH);
        char *path = (char*)patharr.buff;
        shared_ptr<File> base = curproc->ofile(a_basefd);
        if(base == nullptr) { return -EBADF; }

        return Path(path, base).pathOpen(a_flags, a_mode);
    }
    sysrt_t close(int a_fd) {
        auto curproc = kHartObj().curtask->getProcess();
        if(curproc->ofile(a_fd) == nullptr) { return -EBADF; } // 不能关闭一个已关闭的文件
        // 不能用新的局部变量代替，局部变量和files[a_fd]是两个不同的SharedPtr
        curproc->files[a_fd].reset();

        return statcode::ok;
    }
    sysrt_t pipe2(int fd,int flags){
        auto &cur=kHartObj().curtask;
        auto proc=cur->getProcess();
        auto pipe=make_shared<pipe::Pipe>();
        auto rfile=make_shared<File>(pipe,fs::FileOp::read);
        auto wfile=make_shared<File>(pipe,fs::FileOp::write);
        int fds[]={proc->fdAlloc(rfile),proc->fdAlloc(wfile)};
        proc->vmar[fd]<<fds;
        return statcode::ok;
    }
    sysrt_t getDents64(int a_dirfd,DStat *a_buf,size_t a_len) {
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
    sysrt_t lSeek(int fd,off_t offset,int whence) {
        auto &ctx = kHartObj().curtask->ctx;

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(fd);
        if(!file) { return -EBADF; }

        return file->lSeek(offset, whence);
    }
    sysrt_t read(int fd,addr_t uva,size_t len){
        auto file = kHartObj().curtask->getProcess()->ofile(fd);
        if(file == nullptr) { return -EBADF; }
        auto buf_=new uint8_t[len];
        ByteArray buf(buf_,len);
        auto rdbytes=file->read(buf);
        kHartObj().curtask->getProcess()->vmar[uva] << ByteArray(buf_,rdbytes);
        delete[] buf_;
        return rdbytes;
    }
    sysrt_t write(int a_fd,addr_t a_src,size_t a_len){
        if(!a_src) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }

        return file->write(curproc->vmar.copyin(a_src, a_len));
    }
    sysrt_t sendFile(int a_outfd,int a_infd,off_t *a_offset,size_t a_len) {
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
    sysrt_t readLinkAt(int a_basefd,const char *a_path,char *a_buf,size_t a_bufsiz) {
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
    sysrt_t fStatAt(int a_basefd,const char *a_path,KStat *a_kst,int a_flags) {
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
    sysrt_t fStat(int a_fd,KStat *a_kst) {
        if(a_kst==nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        shared_ptr<File> file = curproc->ofile(a_fd);
        if(file == nullptr) { return -EBADF; }
        curproc->vmar.copyout((xlen_t)a_kst, ByteArray((uint8*)&file->obj.kst(), sizeof(KStat)));
        // @bug 用户态读到的数据混乱
        return statcode::ok;
    }
    sysrt_t sync() {
        // 现阶段实现为write-through缓存，自动同步
        return statcode::ok;
    }
} // namespace syscall
