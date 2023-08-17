#include "common.h"
#include "kernel.hh"
#include "ipc.hh"

#include <linux/sched.h>

namespace syscall
{
    using namespace sys;
    using namespace signal;
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

    sysrt_t sigAction(int sig,SigAct *a_nact,SigAct *a_oact) {
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray oactarr(sizeof(SigAct));
        SigAct *oact = (SigAct*)oactarr.buff;
        if(a_oact == nullptr) { oact = nullptr; }
        SigAct *nact = nullptr;
        int ret = statcode::err;
        if(a_nact != nullptr ) {
            ByteArray nactarr = curproc->vmar.copyin((xlen_t)a_nact, sizeof(SigAct));
            nact = (SigAct*)nactarr.buff;
            ret = signal::sigAction(sig, nact, oact);
        }
        else { ret = signal::sigAction(sig, nact, oact); }
        if(ret==0 && a_oact!=nullptr) { curproc->vmar.copyout((xlen_t)a_oact, oactarr); }

        return ret;
    }
    sysrt_t sigProcMask(int a_how,addr_t a_nset,addr_t a_oset,size_t a_sigsetsize) {
        auto curproc = kHartObj().curtask->getProcess();
        if(!a_nset) return 0;
        SigSet nset,oset;
        curproc->vmar[(addr_t)a_nset]>> nset;
        int ret = signal::sigProcMask(a_how, &nset, &oset, a_sigsetsize);
        if(a_oset) curproc->vmar[(addr_t)a_oset]<<oset;
        return ret;
    }


} // namespace syscall
