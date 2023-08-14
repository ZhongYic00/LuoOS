#include "kernel.hh"
#include <EASTL/chrono.h>
#include <sys/time.h>
#include <sys/times.h>

namespace syscall
{
    using sys::statcode;
    int clock_gettime (clockid_t __clock_id, struct timespec *__tp){
        /// @note @bug clockid is ignored
        if(__tp){
            auto curproc = kHartObj().curtask->getProcess();
            auto cur=eastl::chrono::system_clock::now();
            auto ticks=cur.time_since_epoch().count();
            struct timespec ts = { ticks/kernel::CLK_FREQ, ((100000*ticks/kernel::CLK_FREQ)%100000)*10 };
            curproc->vmar[(xlen_t)__tp]<<ts;
        }
        return statcode::ok;
    }
    int gettimeofday (struct timeval *tv,struct timezone *tz){
        auto curproc = kHartObj().curtask->getProcess();
        if(tv){
            auto cur=eastl::chrono::system_clock::now();
            auto ticks=cur.time_since_epoch().count();
            struct timespec ts = { ticks/kernel::CLK_FREQ, ((100000*ticks/kernel::CLK_FREQ)%100000)*10 };
            curproc->vmar[(xlen_t)tv]<<ts;
        }
        /// @note tz is ignored
        return statcode::ok;
    }
    sysrt_t times(struct tms *buf){
        if(!buf) return -EFAULT;
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar[(addr_t)buf]<<curproc->stats.ti;
        return kHartObj().g_ticks;
    }
} // namespace syscall
