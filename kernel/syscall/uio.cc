#include <linux/uio.h>
#include <asm/errno.h>
#include <sys/select.h>
#include <asm/poll.h>
#include "common.h"
#include "kernel.hh"
#include "time.hh"
#include "scatterio.hh"

namespace syscall
{
    sysrt_t readv(int fd, xlen_t iov, int iovcnt){
        auto curproc=kHartObj().curtask->getProcess();
        auto file = kHartObj().curtask->getProcess()->ofile(fd);
        if(file == nullptr) { return -EBADF; }

        auto ustream=curproc->vmar[iov];
        vector<Slice> memvecs;
        for(iovec vec;iovcnt--;){
            ustream>>vec;
            memvecs.push_back(Slice{(xlen_t)vec.iov_base,(xlen_t)vec.iov_base+vec.iov_len-1});
        }
        UMemScatteredIO umio(curproc->vmar,memvecs);
        return file->readv(umio);
    }
    sysrt_t writev(int fd, xlen_t iov, int iovcnt){
        auto curproc=kHartObj().curtask->getProcess();
        auto file=curproc->ofile(fd);
        if(file == nullptr) { return -EBADF; }

        auto ustream=curproc->vmar[iov];
        vector<Slice> memvecs;
        for(iovec vec;iovcnt--;){
            ustream>>vec;
            memvecs.push_back(Slice{(xlen_t)vec.iov_base,(xlen_t)vec.iov_base+vec.iov_len-1});
        }

        UMemScatteredIO umio(curproc->vmar,memvecs);
        return file->writev(umio);
    }
    sysrt_t pselect(int nfds, fd_set *readfds, fd_set *writefds,
        fd_set *exceptfds, const struct timespec *timeout,
        const sigset_t *sigmask){
        int rt=0;
        fd_set rfds,wfds,efds;
        auto curproc=kHartObj().curtask->getProcess();
        if(readfds)curproc->vmar[(addr_t)readfds]>>rfds;
        if(writefds)curproc->vmar[(addr_t)writefds]>>wfds;
        if(exceptfds)curproc->vmar[(addr_t)exceptfds]>>efds;
        timespec tmout;
        if(timeout)curproc->vmar[(addr_t)timeout]>>tmout;
        auto expired=eastl::chrono::system_clock::now()+timeservice::timespec2duration(tmout);
        do{
            for(int i=0;i<nfds;i++){
                if(readfds && FD_ISSET(i,&rfds)){
                    if(curproc->ofile(i)->isRReady())
                        rt++;
                    else
                        FD_CLR(i,&rfds);
                }
                if(writefds && FD_ISSET(i,&wfds)){
                    if(curproc->ofile(i)->isWReady())
                        rt++;
                    else
                        FD_CLR(i,&wfds);
                }
                if(exceptfds && FD_ISSET(i,&efds)){
                }
            }
            if(rt) break;
            kernel::yield();
        }while(!timeout || eastl::chrono::system_clock::now()<expired);
        if(readfds)curproc->vmar[(addr_t)readfds]<<rfds;
        if(writefds)curproc->vmar[(addr_t)writefds]<<wfds;
        if(exceptfds)curproc->vmar[(addr_t)exceptfds]<<efds;
        return rt;
    }
    sysrt_t pPoll(struct pollfd *a_fds, int nfds,
               const struct timespec *tmo_p, const sigset_t *sigmask,size_t sigsetsize){
        if(a_fds==nullptr || nfds<0 || sigsetsize!=sizeof(sigset_t)) { return -EINVAL; }
        
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray fdsarr = curproc->vmar.copyin((xlen_t)a_fds, nfds*sizeof(pollfd));
        pollfd *fds = (pollfd*)fdsarr.buff;
        // @todo: 处理信号阻塞
        // @todo: 处理等待时间（类似nanoSleep）
        int ret = 0;
        for (xlen_t i = 0; i < nfds; ++i) {  // @todo: 需要完善
            fds[i].revents = 0;
            if (fds[i].fd < 0) { continue; }
            if (fds[i].events & POLLIN) { fds[i].revents |= POLLIN; }  // @todo: we assume this is always available
            if (fds[i].events & POLLOUT) { fds[i].revents |= POLLOUT; }  // @todo: we assume this is always available
            ++ret;
        }
        curproc->vmar.copyout((xlen_t)a_fds, fdsarr);

        return ret;
    }
} // namespace syscall
