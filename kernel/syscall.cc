#include "syscall.hh"
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
#include <sys/select.h>
#include <asm/poll.h>
using nonstd::expected;

#define moduleLevel LogLevel::info


extern void _strapexit();
extern char _uimg_start;
namespace syscall {
    syscall_t syscallPtrs[sys::syscalls::nSyscalls];

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
    sysrt_t testFATInit();

    sysrt_t exit(int status);
    sysrt_t exitGroup(int status);

    sysrt_t futex(int *uaddr, int futex_op, int val,
                 const struct timespec *timeout,   /* or: uint32_t val2 */
                 int *uaddr2, int val3);
    sysrt_t nanoSleep(const struct timespec *req, struct timespec *rem);
    sysrt_t clock_gettime (clockid_t __clock_id, struct timespec *__tp);
    sysrt_t kill(pid_t pid, int sig);
    sysrt_t tkill(int tid, int sig);

    sysrt_t clone(unsigned long flags, void *stack,
                    int *parent_tid, unsigned long tls,
                    int *child_tid);
    sysrt_t execve(addr_t pathuva,addr_t argv,addr_t envp);
    sysrt_t setTidAddress(int *tidptr);
    sysrt_t getRLimit(int a_rsrc,RLim *a_rlim);
    sysrt_t setRLimit(int a_rsrc,RLim *a_rlim);
    sysrt_t getrusage(int who, struct ::rusage *usage);
    sysrt_t uMask(mode_t a_mask);
    sysrt_t gettimeofday (struct timeval *__restrict __tv,
			 struct timezone *__tz) __THROW __nonnull ((1));
    sysrt_t getPid();
    sysrt_t getPPid();
    sysrt_t getUid();
    sysrt_t getEuid();
    sysrt_t getGid();
    sysrt_t getEgid();
    sysrt_t getTid();
    sysrt_t setGid(gid_t a_gid);
    sysrt_t setUid(uid_t a_uid);
    sysrt_t getRESgid(gid_t *a_rgid,gid_t *a_egid,gid_t *a_sgid);
    sysrt_t getRESuid(uid_t *a_ruid,uid_t *a_euid,uid_t *a_suid);
    sysrt_t setPGid(pid_t a_pid,pid_t a_pgid);
    sysrt_t getPGid(pid_t a_pid);
    sysrt_t getSid(pid_t a_pid);
    sysrt_t setSid();
    sysrt_t getGroups(int a_size,gid_t *a_list);
    sysrt_t setGroups(int a_size,gid_t *a_list);
    sysrt_t times(struct ::tms *buf);

    sysrt_t brk(addr_t addr);
    sysrt_t mmap(addr_t addr,size_t len,int prot,int flags,int fd,int offset);
    sysrt_t munmap(addr_t addr,size_t len);
    sysrt_t mremap(addr_t oldaddr, size_t oldsize,size_t newsize, int flags, addr_t newaddr);
    sysrt_t mprotect(addr_t addr,size_t len,int prot);
    sysrt_t madvise(addr_t addr,size_t length,int advice);
    sysrt_t mlock(addr_t addr, size_t len);
    sysrt_t munlock(addr_t addr, size_t len);

    sysrt_t getCwd(char *a_buf,size_t a_len);
    sysrt_t dup(int a_fd);
    sysrt_t dup3(int a_fd,int a_newfd,int a_flags);
    sysrt_t fCntl(int a_fd,int a_cmd,uint64_t a_arg);
    sysrt_t ioCtl(int a_fd,uint64_t a_request,addr_t a_arg);
    sysrt_t mkDirAt(int a_basefd,const char *a_path,mode_t a_mode);
    sysrt_t unlinkAt(int a_basefd,const char *a_path,int a_flags);
    sysrt_t symLinkAt(const char *a_target,int a_basefd,const char *a_linkpath);
    sysrt_t linkAt(int a_oldbasefd,const char *a_oldpath,int a_newbasefd,const char *a_newpath,int a_flags);
    sysrt_t umount2(const char *a_devpath,int a_flags);
    sysrt_t mount(const char *a_devpath,const char *a_mountpath, const char *a_fstype, uint64_t flags, const void *a_data);
    sysrt_t statFS(const char *a_path,StatFS *a_buf);
    sysrt_t ftruncate(unsigned int fd, loff_t length);
    sysrt_t fAccessAt(int a_basefd,const char *a_path,int a_mode,int a_flags);
    sysrt_t chDir(const char *a_path);
    sysrt_t fChDir(int a_fd);
    sysrt_t fChMod(int a_fd,mode_t a_mode);
    sysrt_t fChModAt(int a_basefd,const char *a_path,mode_t a_mode,int a_flags);
    sysrt_t fChOwnAt(int a_basefd,const char *a_path,uid_t a_uid,gid_t a_gid,int a_flags);
    sysrt_t fChOwn(int a_fd,uid_t a_uid,gid_t a_gid);
    sysrt_t openAt(int a_basefd,const char *a_path,int a_flags,mode_t a_mode);
    sysrt_t close(int a_fd);
    sysrt_t pipe2(int fd,int flags);
    sysrt_t getDents64(int a_dirfd,DStat *a_buf,size_t a_len);
    sysrt_t lSeek(int fd,off_t offset,int whence);
    sysrt_t read(int fd,addr_t uva,size_t len);
    sysrt_t write(int a_fd,addr_t a_src,size_t a_len);
    sysrt_t readv(int fd, xlen_t iov, int iovcnt);
    sysrt_t writev(int fd, xlen_t iov, int iovcnt);
    sysrt_t sendFile(int a_outfd,int a_infd,off_t *a_offset,size_t a_len);
    sysrt_t pselect(int nfds, fd_set *readfds, fd_set *writefds,
                   fd_set *exceptfds, const struct timespec *timeout,
                   const sigset_t *sigmask);
    sysrt_t pPoll(struct pollfd *a_fds, int nfds,
               const struct timespec *tmo_p, const sigset_t *sigmask,size_t sigsetsize);
    sysrt_t readLinkAt(int a_basefd,const char *a_path,char *a_buf,size_t a_bufsiz);
    sysrt_t fStatAt(int a_basefd,const char *a_path,KStat *a_kst,int a_flags);
    sysrt_t fStat(int a_basefd,KStat *a_kst);
    sysrt_t sync();

    sysrt_t copyfilerange(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len, unsigned int flags);

static constexpr int SEEK_SET = 0;
static constexpr int SEEK_CUR = 1;
static constexpr int SEEK_END = 2;
    sysrt_t copyfilerange(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len, unsigned int flags){
        Log(info,"copyfilerange fin=%d inoff=%x fout=%d outoff=%x len=%d",fd_in,off_in,fd_out,off_out,len);
        auto curproc=kHartObj().curtask->getProcess();
        auto fin=curproc->ofile(fd_in);
        auto fout=curproc->ofile(fd_out);
        auto offinOld=fin->lSeek(0,SEEK_CUR),offoutOld=fout->lSeek(0,SEEK_CUR);
        loff_t offin=offinOld,offout=offoutOld;
        if(off_in)curproc->vmar[(addr_t)off_in]>>offin;
        if(off_out)curproc->vmar[(addr_t)off_out]>>offout;
        if(fin->lSeek(offin,SEEK_SET)<0) return 0;
        if(fout->lSeek(offout,SEEK_SET)<0) return 0;
        auto buf_=(uint8_t*)vm::pn2addr(kGlobObjs->pageMgr->alloc(1));
        size_t cpbytes=0;
        while (len > 0) {
            ssize_t rdbytes = klib::min(vm::pageSize,len);
            rdbytes=fin->read(ByteArray{buf_,rdbytes});  // @todo: 安全检查
            if(rdbytes==0)break;
            int wbytes = fout->write(ByteArray(buf_,rdbytes));
            if (wbytes < 0) return make_unexpected(-wbytes);
            cpbytes += wbytes;
            if (rdbytes != wbytes) { break; } // EOF reached in in_fd or out_fd is full
            len -= rdbytes;
        }
        kGlobObjs->pageMgr->free(vm::addr2pn((xlen_t)buf_),0);
        return cpbytes;
    }
    sysrt_t ftruncate(unsigned int fd, loff_t length){
        auto file=kHartObj().curtask->getProcess()->ofile(fd);
        if(file->obj.rType()!=fs::FileType::entry) return Err(EINVAL);
        if(S_ISDIR(file->obj.getEntry()->getINode()->rMode())) return Err(EISDIR);
        file->obj.getEntry()->getINode()->nodTrunc();
        return statcode::ok;
    }

    sysrt_t sysyield(){
        kernel::yield();
        return statcode::ok;
    }
    sysrt_t sigAction(int sig,SigAct *a_nact,SigAct *a_oact);
    sysrt_t sigProcMask(int a_how,addr_t a_nset,addr_t a_oset,size_t a_sigsetsize);
    sysrt_t sigReturn(){
        return signal::sigReturn();
    }
    sysrt_t uName(addr_t a_uts) {
        if(!a_uts) { return -EFAULT; }
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar[a_uts]<<kInfo.uts;
        return statcode::ok;
    }
    sysrt_t waitpid(pid_t pid,xlen_t wstatus,int options);
    sysrt_t wait(pid_t pid,uint64_t wstatus){
        return waitpid(pid,wstatus,0);
    }
    sysrt_t syncFS(){
        // 同sync()
        return statcode::ok;
    }
    sysrt_t reboot1(int magic,int magic2,int cmd){
        if(!(magic==LINUX_REBOOT_MAGIC1 && magic2==LINUX_REBOOT_MAGIC2))
            return -1;// return nonstd::make_unexpected("magic num unmatched!");
        if(cmd==LINUX_REBOOT_CMD_POWER_OFF){
            Log(error,"LuoOS Shutdown! Bye-Bye");
            sbi_shutdown();
        }
        return statcode::err;
    }
    inline sysrt_t membarrier(int cmd,int flags){
        return 0;
    }
const char *syscallHelper[sys::syscalls::nSyscalls];
#define DECLSYSCALL(x,ptr) syscallPtrs[x]=reinterpret_cast<syscall_t>(ptr);syscallHelper[x]=#x;
    void init(){
        using scnum=sys::syscalls;
        for(int i=0;i<scnum::nSyscalls;i++)syscallHelper[i]="";
        DECLSYSCALL(scnum::testfatinit,testFATInit);
        DECLSYSCALL(scnum::reboot,reboot1);
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
        DECLSYSCALL(scnum::pselect,pselect);
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
        DECLSYSCALL(scnum::membarrier,membarrier);
        DECLSYSCALL(scnum::copyfilerange,copyfilerange);
    }
} // namespace syscall
