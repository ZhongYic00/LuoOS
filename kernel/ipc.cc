#include "ipc.hh"
#include "kernel.hh"
#include "errno.h"
// #define moduleLevel LogLevel::debug

namespace pipe
{

    void Pipe::sleep(){
        waiting.push_back(kHartObj().curtask);
        syscall::sleep();
    }
    void Pipe::wakeup(){
        for(auto task:waiting){
            Log(debug,"Pipe::wakeup Task<%d>\n",task->id);
            kGlobObjs->scheduler->wakeup(task);
        }
        while(!waiting.empty())waiting.pop_front();
    }
} // namespace ipc

namespace signal
{
    void send(Process &proc,int num,unique_ptr<SignalInfo>& info){
        for(auto tsk:proc.tasks){
            if(!tsk->block[num])
                return send(*tsk,num,info); 
        }
    }
    void send(Task &task,int num,unique_ptr<SignalInfo>& info){
        if(!task.pending[num]){
            task.pendingmask[num]=1;
            task.pending[num]=std::move(info);
        }
        return ;
    }
    xlen_t doSigAction(int a_sig, SignalAction *a_act, SignalAction *a_oact) {
        if (a_sig <= 0 || a_sig > numSignals || a_sig == SIGKILL || a_sig == SIGSTOP) return -EINVAL;
        auto curproc = kHartObj().curtask->getProcess();
        if (a_oact != nullptr) { *a_oact = curproc->actions[a_sig-1]; }
        if (a_act != nullptr) { curproc->actions[a_sig-1] = *a_act; }
        return 0;
    }
    xlen_t doSigProcMask(int a_how, SigSet *a_nset, SigSet *a_oset, size_t a_sigsetsize) {
        if (a_sigsetsize != sizeof(SigSet)) return -EINVAL;
        auto cur = kHartObj().curtask;
        if (a_oset != nullptr) { a_oset->sig[0] = cur->block.to_ulong(); }
        if (a_nset == nullptr) return 0;
        switch (a_how) {
            case SIG_BLOCK:
                cur->block |= a_nset->sig[0];
                break;
            case SIG_UNBLOCK:
                cur->block &= ~(a_nset->sig[0]);
                break;
            case SIG_SETMASK:
                cur->block = a_nset->sig[0];
                break;
            default:
                return -EINVAL;
        }
        cur->block &= ~((1UL << (SIGKILL - 1)) | (1UL << (SIGSTOP - 1)));
        return 0;
    }
} // namespace signal
