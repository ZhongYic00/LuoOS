#include "common.h"
#include "kernel.hh"

#include <linux/sched.h>

namespace syscall
{
    long clone(unsigned long flags, void *stack,
                      int *parent_tid, int *child_tid,
                      unsigned long tls){
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
        if(flags&CLONE_CHILD_SETTID)
            thrd->getProcess()->vmar[(addr_t)child_tid]<<thrd->tid();
        if(flags&CLONE_PARENT_SETTID)
            cur->getProcess()->vmar[(addr_t)parent_tid]<<thrd->tid();
        return thrd->tid();
    }
    sysrt_t setTidAddress(int *tidptr){
        auto &cur=kHartObj().curtask;
        /// @bug how to use this attr? one-off?
        cur->attrs.setChildTid=tidptr;
        cur->getProcess()->vmar[(addr_t)tidptr]<<cur->id;
        return cur->id;
    }
} // namespace syscall
