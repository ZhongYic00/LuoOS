#include "common.h"
#include "kernel.hh"

#include <linux/sched.h>

namespace syscall
{
    long clone(unsigned long flags, void *stack,
                      int *parent_tid, int *child_tid,
                      unsigned long tls){
        auto &ctx=kHartObj().curtask->ctx;
        if(flags&(CLONE_VM|CLONE_THREAD|CLONE_SIGHAND)){
            auto curproc=kHartObj().curtask->getProcess();
            auto thrd=curproc->newTask(*kHartObj().curtask,(addr_t)stack);
            return thrd->id;
        } else {
            auto pid=proc::clone(kHartObj().curtask);
            auto thrd=(**kGlobObjs->procMgr)[pid]->defaultTask();
            if(stack)thrd->ctx.sp()=(addr_t)stack;
            Log(debug,"clone curproc=%d, new proc=%d",kHartObj().curtask->getProcess()->pid(),pid);
            return pid;
        }
    }
} // namespace syscall
