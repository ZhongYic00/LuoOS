#include "common.h"
#include "kernel.hh"
#include "time.hh"
#include <linux/sched.h>
#include <sys/resource.h>

namespace syscall
{
    using namespace sys;
    long clone(unsigned long flags, void *stack,
                      int *parent_tid, unsigned long tls,
                      int *child_tid){
        auto &cur=kHartObj().curtask;
        auto &ctx=kHartObj().curtask->ctx;

        proc::Task* thrd;
        if(flags&(CLONE_VM|CLONE_THREAD|CLONE_SIGHAND)){
            auto curproc=kHartObj().curtask->getProcess();
            thrd=curproc->newTask(*kHartObj().curtask,(addr_t)stack);
            thrd->ctx.a0()=0;
        } else {
            auto pid=proc::clone(kHartObj().curtask);
            thrd=(**kGlobObjs->procMgr)[pid]->defaultTask();
            if(stack)thrd->ctx.sp()=(addr_t)stack;
            Log(debug,"clone curproc=%d, new proc=%d",kHartObj().curtask->getProcess()->pid(),pid);
        }
        
        // set/clear child tid
        if(flags&CLONE_CHILD_SETTID && child_tid)
            thrd->getProcess()->vmar[(addr_t)child_tid]<<thrd->tid();
        if(flags&CLONE_PARENT_SETTID && parent_tid)
            cur->getProcess()->vmar[(addr_t)parent_tid]<<thrd->tid();
        if(flags&CLONE_SETTLS)
            thrd->ctx.tp()=tls;
        return thrd->tid();
    }
    sysrt_t setTidAddress(int *tidptr){
        auto &cur=kHartObj().curtask;
        /// @bug how to use this attr? one-off?
        cur->attrs.setChildTid=tidptr;
        cur->getProcess()->vmar[(addr_t)tidptr]<<cur->id;
        return cur->id;
    }

    sysrt_t getrusage(int who, struct rusage *usage){
        auto &cur=kHartObj().curtask;
        switch(who){
            case RUSAGE_SELF:{
                auto tms=cur->getProcess()->stats.ti;
                struct rusage usg={
                    .ru_utime=timeservice::duration2timeval(timeservice::ticks2chrono(tms.tms_utime)),
                    .ru_stime={0,0},
                };
                cur->getProcess()->vmar[(addr_t)usage]<<usg;
                return statcode::ok;
            }
            case RUSAGE_CHILDREN:{

                return statcode::ok;
            }
            default:
                Log(error,"who is invalid");
                return -EINVAL;
        }
    }
} // namespace syscall
