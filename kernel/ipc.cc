#include "ipc.hh"
#include "kernel.hh"
// #define moduleLevel LogLevel::debug

namespace pipe
{
} // namespace ipc
shared_ptr<signal::SigAct> defaultSigAct;

extern void sigreturn();
namespace signal
{
    #define SIG_BASE_ERRNO (128)
    static void sigExitHandler(int a_sig) { kHartObj().curtask->getProcess()->exit(SIG_BASE_ERRNO + a_sig); }
    static void sigStopHandler() {
        Log(error, "unimplemented: sigStopHandler\n");
        // auto curproc = kHartObj().curtask->getProcess();
        // if (curproc->sigsstop.empty()) {
        //     curproc->sigmask |= (1UL << (SIGCONT-1));
        //     block(curproc, &curproc->sigsstop);
        //     // @todo: 如何处理？
        // }
    }
    static void sigContHandler() {
        Log(error, "unimplemented: sigContHandler\n");
        // auto curproc = kHartObj().curtask->getProcess();
        // if (!curproc->sigsstop.empty()) {
        //     curproc->sigmask |= (1UL << (SIGSTOP-1)) | (1UL << (SIGTSTP-1)) | (1UL << (SIGTTIN-1)) | (1UL << (SIGTTOU-1));
        //     wakeup(&curproc->sigsstop);
        //     // @todo: 如何处理？
        // }
    }
    static void sigDumpHandler(int a_sig) {
        auto curproc = kHartObj().curtask->getProcess();
        Log(error, "core dump, caused by proc %d, sig %d", curproc->pid(), a_sig);
        sigSend(*curproc->parentProc(), SIGCHLD);
        sigExitHandler(a_sig);
    }
    
    void sigInit() { defaultSigAct = make_shared<SigAct>(); }
    void sigSend(Process &a_proc,int a_sig, shared_ptr<SigInfo> a_info) {
        for(auto tsk: a_proc.tasks){
            if(!sigMaskBit(tsk->sigmask, a_sig)) {
                sigSend(*tsk, a_sig, a_info);
                return;
            }
        }
    }
    void sigSend(Task &a_task,int a_sig, shared_ptr<SigInfo> a_info) {
        if(a_task.siginfos[a_sig] == nullptr){
            // a_task.sigpending[a_sig] = 1;
            sigMaskBitSet(a_task.sigpending, a_sig, 1);
            a_task.siginfos[a_sig] = a_info;
        }
        return;
    }
    xlen_t sigAction(int a_sig, SigAct *a_nact, SigAct *a_oact) {
        if (a_sig <= 0 || a_sig > numSigs || a_sig == SIGKILL || a_sig == SIGSTOP) return -EINVAL;
        auto curproc = kHartObj().curtask->getProcess();
        if (a_oact != nullptr) { *a_oact = *(curproc->getSigAct(a_sig-1)); }
        if (a_nact != nullptr) { curproc->sigacts[a_sig-1] = make_shared<SigAct>(*a_nact); }
        return 0;
    }
    xlen_t sigProcMask(int a_how, SigSet *a_nset, SigSet *a_oset, size_t a_sigsetsize) {
        if (a_sigsetsize != sizeof(SigSet)) { return -EINVAL; }
        auto cur = kHartObj().curtask;
        if (a_oset != nullptr) { a_oset->sig[0] = cur->sigmask; }
        if (a_nset == nullptr) { return 0; }
        switch (a_how) {
            case SIG_BLOCK:
                cur->sigmask |= a_nset->sig[0];
                break;
            case SIG_UNBLOCK:
                cur->sigmask &= ~(a_nset->sig[0]);
                break;
            case SIG_SETMASK:
                cur->sigmask = a_nset->sig[0];
                break;
            default:
                return -EINVAL;
        }
        cur->sigmask &= ~((1UL << (SIGKILL - 1)) | (1UL << (SIGSTOP - 1)));
        return 0;
    }
    xlen_t sigReturn() {
        auto cur = kHartObj().curtask;
        auto &ctx = cur->ctx;
        auto curproc = cur->getProcess();

        uintptr_t *link = reinterpret_cast<uintptr_t*>(ctx.sp());
        uintptr_t sigstack = *(curproc->vmar.copyin((xlen_t)link, sizeof(uintptr_t)).buff);
        // if(sigstack == (uintptr_t)cur->sigstack.ss_sp) { cur->sigstack.ss_onstack = 0; }

        sigstack -= sizeof(SigMask);
        SigMask *oldmask = reinterpret_cast<SigMask*>(sigstack);
        cur->sigmask = *(curproc->vmar.copyin((xlen_t)oldmask, sizeof(SigMask)).buff);

        sigstack -= sizeof(SigCtx);
        SigCtx *context = reinterpret_cast<SigCtx*>(sigstack);
        // for (size_t i = 1; i < NGREG; i++) context->gregs[i] = ctx->gpr[i];
        ByteArray ksigctxarr = curproc->vmar.copyin((xlen_t)context, sizeof(SigCtx));
        SigCtx *ksigctx = (SigCtx*)ksigctxarr.buff;
        memmove((void*)(ctx.gpr), (void*)(((xlen_t)&(ksigctx->sc_regs))+sizeof(xlen_t)), sizeof(ctx.gpr));  // @todo: 写成对象操作
        // ctx->sepc = context->sc_regs[0];
        ctx.pc = ksigctx->sc_regs.pc;

        return 0;
    }
    void sigHandler() {
        auto cur = kHartObj().curtask;
        auto &ctx = cur->ctx;
        auto curproc = cur->getProcess();
        int signum = 0;
        for(int i = 1; i <= numSigs; ++i) {
            if (((1UL<<(i-1))&cur->sigpending) != 0 && ((1UL<<(i-1))&cur->sigmask) == 0) {
                signum = i;
                break;
            }
        }
        if(signum == 0) { return; }
        cur->sigpending &= ~(1UL << (signum-1));
        if(signum == SIGKILL) { sigExitHandler(signum); return; }
        if(signum == SIGSTOP) { sigStopHandler(); return; }
        shared_ptr<SigAct> sa = curproc->getSigAct(signum-1);
        if(sa->sa_handler == SIG_ERR) { sigExitHandler(signum); return; }
        if(sa->sa_handler == SIG_DFL) {
            switch (signum) {
                // 终止当前进程
                case SIGHUP:
                case SIGINT:
                case SIGKILL:
                case SIGPIPE:
                case SIGALRM:
                case SIGTERM:
                case SIGUSR1:
                case SIGUSR2:
                case SIGPOLL:
                case SIGPROF:
                case SIGVTALRM:
                // @todo: 实时信号处理不完全
                case SIGRTMIN ... SIGRTMAX:
                    sigExitHandler(signum);
                    break;
                // 忽略
                case SIGCHLD:
                case SIGURG:
                case SIGPWR:
                    break;
                // 核心转储
                case SIGQUIT:
                case SIGILL:
                case SIGABRT:
                case SIGFPE:
                case SIGSEGV:
                case SIGBUS:
                case SIGSYS:
                case SIGTRAP:
                case SIGXCPU:
                case SIGXFSZ:
                    sigDumpHandler(signum);
                    break;
                // 阻塞
                case SIGSTOP:
                case SIGTSTP:
                case SIGTTIN:
                case SIGTTOU:
                    sigStopHandler();
                    break;
                // 恢复
                case SIGCONT:
                    sigContHandler();
                    break;
                // @todo: 其它系统调用
                default:
                    Log(error, "unimplemented default signal handler: %d\n", signum);
                    sigExitHandler(signum);
                    break;
            }
            return;
        }
        if(sa->sa_handler == SIG_IGN) { return; }
        uintptr_t sigstack = ctx.sp();
        // 启用备用信号堆栈（如果需要）
        bool switch_stack = (sa->sa_flags&SA_ONSTACK) && !cur->onSigStack();
        if(switch_stack) {
            sigstack = (uintptr_t)cur->sigstack.ss_sp;
            Log(error, "SigAltStack enabled at 0x%lx", sigstack);
        }  // @todo: 如果事先未设置sigaltstack，则行为未定义
        else {  // 默认堆栈
            sigstack &= ~0xf;  // 对齐
            sigstack -= 0x80;  // red zone
        }
        uintptr_t signal_stack_base = sigstack;  // @todo: 是否正确？
        // 设置SigSet
        sigstack -= sizeof(SigSet);
        SigSet *oldmask = reinterpret_cast<SigSet*>(sigstack);
        // *oldmask = cur->sigmask;
        curproc->vmar.copyout((xlen_t)oldmask, ByteArray((uint8*)&(cur->sigmask), sizeof(SigSet)));
        if (!(sa->sa_flags & SA_NODEFER)) { cur->sigmask |= 1UL << (1-signum); }
        cur->sigmask |= sa->sa_mask.sig[0];
        // 设置SigCtx
        sigstack -= sizeof(SigCtx);
        SigCtx *context = reinterpret_cast<SigCtx*>(sigstack);
        SigCtx ksigctx;
        // for (size_t i = 1; i < NGREG; i++) context->gregs[i] = ctx->gpr[i];
        memmove((void*)(((xlen_t)&(ksigctx.sc_regs))+sizeof(xlen_t)), (void*)ctx.gpr, sizeof(ctx.gpr));
        // context->gregs[0] = ctx->sepc;
        ksigctx.sc_regs.pc = ctx.pc;  // pret_code
        curproc->vmar.copyout((xlen_t)context, ByteArray((uint8*)&ksigctx, sizeof(SigCtx)));
        // 设置UCtx（意义不明）
        SigInfo *info = nullptr;
        UCtx *ucontext = nullptr;
        if (sa->sa_flags & SA_SIGINFO) {
            // 设置SigInfo
            sigstack -= sizeof(SigInfo);
            info = reinterpret_cast<SigInfo*>(sigstack);
            if (cur->siginfos[signum-1] == nullptr) {
                SigInfo ksiginfo;
                ksiginfo.si_signo = signum;
                ksiginfo.si_code = 0;
                ksiginfo.si_errno = 0;
                curproc->vmar.copyout((xlen_t)info, ByteArray((uint8*)&ksiginfo, sizeof(SigInfo)));
            }
            else {
                // *info = *cur->siginfos[signum-1];
                curproc->vmar.copyout((xlen_t)info, ByteArray((uint8*)(cur->siginfos[signum-1].get()), sizeof(SigInfo)));
                cur->siginfos[signum-1].reset();
            }
            // 设置UCtx
            sigstack -= sizeof(UCtx);
            ucontext = reinterpret_cast<UCtx*>(sigstack);
            UCtx kuctx;
            kuctx.uc_flags = 0;
            kuctx.uc_link = nullptr; 
            // FIXME: Is user stack size limited to LARGE_PAGE_SIZE ?
            // FIXME: Alt stack size ?
            // ucontext->uc_stack = SigStack { reinterpret_cast<void *>(sigstack), 0, cur->sigstack.ss_onstack ? sigstack-(reinterpret_cast<uintptr_t>(cur->sigstack.ss_sp)-vm::pageSize) : cur->kernel_sp - (USER_STACK - LARGE_PAGE_SIZE) };
            kuctx.uc_stack = switch_stack ? cur->sigstack : SigStack({ (void*)signal_stack_base, 0, sigStackSiz });
            // kuctx.uc_stack.ss_sp = reinterpret_cast<void*>(sigstack); ？？
            kuctx.uc_sigmask.sig[0] = cur->sigmask;
            // Log(error, "unimplemented: siginfos.ucontext.uc_context\n");
            memmove((void*)(((xlen_t)&(kuctx.uc_mcontext.sc_regs))+sizeof(xlen_t)), (void*)ctx.gpr, sizeof(ctx.gpr));
            kuctx.uc_mcontext.sc_regs.pc = ctx.pc;  // pret_code
            curproc->vmar.copyout((xlen_t)ucontext, ByteArray((uint8*)&kuctx, sizeof(UCtx)));
        }
        else {
            if (cur->siginfos[signum-1] != nullptr) { cur->siginfos[signum-1].reset(); }
        }
        // 存储信号栈底地址
        sigstack -= sizeof(uintptr_t);
        sigstack &= ~0xf;
        uintptr_t *link = reinterpret_cast<uintptr_t*>(sigstack);
        // *link = signal_stack_base;
        curproc->vmar.copyout((xlen_t)link, ByteArray((uint8*)&sigstack, sizeof(uintptr_t)));
        // 切换信号处理上下文
        if(sa->sa_restorer != nullptr) { ctx.x(REG_RA) = reinterpret_cast<long>(sa->sa_restorer); }  // @todo: 设置了SA_SIGINFO时restorer发生改变
        else { ctx.ra() = proc::vDSOfuncAddr(sigreturn); }  // @todo: VSDO
        ctx.x(REG_SP) = sigstack;
        ctx.x(REG_A0) = signum;
        if(sa->sa_flags & SA_SIGINFO) {
            ctx.x(REG_A1) = reinterpret_cast<long>(info);
            ctx.x(REG_A2) = reinterpret_cast<long>(ucontext);
        }
        ctx.pc = reinterpret_cast<long>(sa->sa_handler);
        if(sa->sa_flags & SA_RESETHAND) {
            if(curproc->sigacts[signum-1] != nullptr) { curproc->sigacts[signum-1].reset(); }
        }
    }
} // namespace signal
