#include "common.h"
#include "kernel.hh"

#include <linux/sched.h>

namespace syscall
{
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
        if(flags&CLONE_CHILD_CLEARTID)
            thrd->attrs.clearChildTid=child_tid;
        return flags&CLONE_THREAD?thrd->tid():thrd->getProcess()->pid();
    }
    sysrt_t setTidAddress(int *tidptr){
        auto &cur=kHartObj().curtask;
        /// @bug how to use this attr? one-off?
        cur->attrs.clearChildTid=tidptr;
        return cur->id;
    }
    sysrt_t exitGroup(int status){
        auto cur=kHartObj().curtask;
        auto curproc=cur->getProcess();
        for(auto task:curproc->tasks){
            task->state=sched::Zombie;
            kGlobObjs->scheduler->remove(task);
        }
        curproc->exit(status);
        yield();
        return 0;
    }
    sysrt_t exit(int status){
        auto cur=kHartObj().curtask;
        cur->exit(status);
        syscall::yield();
        return 0;
    }
} // namespace syscall
