#include "ipc.hh"
#include "kernel.hh"
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
    void sigSend(Process &proc,int num,unique_ptr<SigInfo>& info){
        for(auto tsk:proc.tasks){
            if(!tsk->sigmasks[num])
                return sigSend(*tsk,num,info); 
        }
    }
    void sigSend(Task &task,int num,unique_ptr<SigInfo>& info){
        if(!task.siginfos[num]){
            task.sigpendings[num]=1;
            task.siginfos[num]=std::move(info);
        }
        return ;
    }
    xlen_t doSigAction(int a_sig, SigAct *a_nact, SigAct *a_oact) {
        if (a_sig <= 0 || a_sig > numSignals || a_sig == SIGKILL || a_sig == SIGSTOP) return -EINVAL;
        auto curproc = kHartObj().curtask->getProcess();
        if (a_oact != nullptr) { *a_oact = curproc->sigacts[a_sig-1]; }
        if (a_nact != nullptr) { curproc->sigacts[a_sig-1] = *a_nact; }
        return 0;
    }
    xlen_t doSigProcMask(int a_how, SigSet *a_nset, SigSet *a_oset, size_t a_sigsetsize) {
        if (a_sigsetsize != sizeof(SigSet)) return -EINVAL;
        auto cur = kHartObj().curtask;
        if (a_oset != nullptr) { a_oset->sig[0] = cur->sigmasks.to_ulong(); }
        if (a_nset == nullptr) return 0;
        switch (a_how) {
            case SIG_BLOCK:
                cur->sigmasks |= a_nset->sig[0];
                break;
            case SIG_UNBLOCK:
                cur->sigmasks &= ~(a_nset->sig[0]);
                break;
            case SIG_SETMASK:
                cur->sigmasks = a_nset->sig[0];
                break;
            default:
                return -EINVAL;
        }
        cur->sigmasks &= ~((1UL << (SIGKILL - 1)) | (1UL << (SIGSTOP - 1)));
        return 0;
    }
    // @todo: stack_t是否等价于sigstack
    xlen_t doSigReturn() {
        auto cur = kHartObj().curtask;
        auto &ctx = cur->ctx;
        uintptr_t *link = reinterpret_cast<uintptr_t *>(ctx.gpr[2]);  // REG_SP = 2
        uintptr_t sigstack = *link;
        if (sigstack == (uintptr_t)cur->sigstack.ss_sp) { cur->sigstack.ss_flags = 0; }
        sigstack -= sizeof(SigMask);
        SigMask *oldmask = reinterpret_cast<SigMask*>(sigstack);
        cur->sigmasks = *oldmask;
        sigstack -= sizeof(SigCtx);
        SigCtx *context = reinterpret_cast<SigCtx*>(sigstack);
        // NGREG = 32
        memmove((void*)(ctx.gpr), (void*)&(context->sc_regs), sizeof(ctx.gpr));
        // ctx->sepc = context->sc_regs[0];
        csrWrite(sepc, context->sc_regs.pc);  // @todo: 不确定是否等效
        return 0;
    }
} // namespace signal
