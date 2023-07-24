#include <linux/uio.h>
#include <asm/errno.h>
#include "common.h"
#include "kernel.hh"
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
} // namespace syscall
