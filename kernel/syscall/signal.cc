#include "common.h"
#include "kernel.hh"

#include <linux/sched.h>

namespace syscall
{
    using namespace sys;
    sysrt_t tkill(int tid, int sig){
        signal::sigSend(*(**kGlobObjs->taskMgr)[tid],sig);
        return statcode::ok;
    }
    sysrt_t kill(pid_t pid, int sig){
        /// @todo: 填充SigInfo？
        using namespace signal;
        auto curproc = kHartObj().curtask->getProcess();
        if(pid == 0) { pid = curproc->pid(); }  // @todo: 进程组信号管理
        if(pid < -1) { pid = -pid; }  // @todo: 进程组信号管理
        if(pid > 0) {
            auto proc = (**kGlobObjs->procMgr)[pid];
            if(proc == nullptr) { return -ESRCH; }
            if(sig == 0) { return 0; }
            sigSend(*proc, sig);
            return statcode::ok;
        }
        if(pid == -1) {
            if(sig == 0) { return statcode::ok; }
            else if(sig==SIGKILL || sig==SIGSTOP) { return -EPERM; }
            bool success = false;
            auto procs = (**kGlobObjs->procMgr);
            int procsnum = procs.getObjNum();
            for (int i = 0; i < procsnum; ++i) {
                auto it = procs[i];
                if(it!=nullptr && it!=curproc && it->pid()>2) {  // @todo: 内核进程写成宏
                    success = true;
                    sigSend(*it, sig);
                }
            }
            return success ? statcode::ok : statcode::err;
        }
        return statcode::ok;
    }
} // namespace syscall
