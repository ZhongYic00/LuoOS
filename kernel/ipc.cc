#include "ipc.hh"
#include "kernel.hh"
#define moduleLevel LogLevel::debug

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
            if(!sigMaskBit(tsk->sigmask, a_sig-1)) {
                sigSend(*tsk, a_sig, a_info);
                return;
            }
        }
    }
    void sigSend(Task &a_task,int a_sig, shared_ptr<SigInfo> a_info) {
        if(a_task.siginfos[a_sig-1] == nullptr){
            // a_task.sigpending[a_sig-1] = 1;
            sigMaskBitSet(a_task.sigpending, a_sig-1, 1);
            a_task.siginfos[a_sig-1] = a_info;
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

        addr_t sigstack;
        curproc->vmar[ctx.sp()]>>sigstack;
        // if(sigstack == (uintptr_t)cur->sigstack.ss_sp) { cur->sigstack.ss_onstack = 0; }
        auto ustream=curproc->vmar[sigstack];
        ustream.reverse=true;
        sigcontext sigctx;
        ustream>>cur->sigmask;
        Log(debug,"sigmask@%x",ustream.addr());
        ustream>>sigctx;
        Log(debug,"sigctx@%x",ustream.addr());
        memmove(ctx.gpr,&sigctx.sc_regs.ra, sizeof(ctx.gpr));  // @todo: 写成对象操作
        ctx.pc = sigctx.sc_regs.pc;

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
        Log(error, "dealing SIG_%d", signum);
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
        // 启用备用信号堆栈（如果需要）
        addr_t sigstack=ctx.sp();
        bool switch_stack = (sa->sa_flags&SA_ONSTACK) && !cur->onSigStack();
        if(switch_stack){
            sigstack=(addr_t)cur->sigstack.ss_sp;
            Log(debug, "SigAltStack enabled at 0x%lx", sigstack);
        }  // @todo: 如果事先未设置sigaltstack，则行为未定义
        auto ustream=curproc->vmar[sigstack];
        ustream.reverse=true;
        ustream<<(xlen_t)0x0;
        sigstack=ustream.addr();

        // 设置SigSet
        ustream<<cur->sigmask;
        if (!(sa->sa_flags & SA_NODEFER)) { cur->sigmask |= 1UL << (1-signum); }
        cur->sigmask |= sa->sa_mask.sig[0];
        Log(debug,"sigmask@%x",ustream.addr());
        // 设置SigCtx
        SigCtx ksigctx;
        memmove(&ksigctx.sc_regs.ra,ctx.gpr,sizeof(ctx.gpr));
        ksigctx.sc_regs.pc = ctx.pc;  // pret_code
        ustream<<ksigctx;
        Log(debug,"sigctx@%x",ustream.addr());
        // 设置UCtx（意义不明）
        addr_t pinfo=0,puctx=0;
        if (sa->sa_flags & SA_SIGINFO) {
            // 设置SigInfo
            if (cur->siginfos[signum-1] == nullptr) {
                SigInfo info;
                info.si_signo = signum;
                info.si_code = 0;
                info.si_errno = 0;
                cur->siginfos[signum-1]=make_unique<SigInfo>();
                *cur->siginfos[signum-1]=info;
            }
            ustream<<*cur->siginfos[signum-1];
            pinfo=ustream.addr();
            cur->siginfos[signum-1].reset();
            // 设置UCtx
            UCtx uctx;
            uctx.uc_flags = 0;
            uctx.uc_link = nullptr; 
            uctx.uc_stack = switch_stack ? cur->sigstack : SigStack({ (void*)sigstack, 0, sigStackSiz });
            uctx.uc_sigmask.sig[0] = cur->sigmask;
            // Log(error, "unimplemented: siginfos.ucontext.uc_context\n");
            uctx.uc_mcontext=ksigctx;
            ustream<<uctx;
            puctx=ustream.addr();
        } else {
            if (cur->siginfos[signum-1] != nullptr) { cur->siginfos[signum-1].reset(); }
        }
        // 存储信号栈底地址
        ustream<<sigstack;
        // 切换信号处理上下文
        if(sa->sa_restorer != nullptr) {
            ctx.x(REG_RA) = reinterpret_cast<long>(sa->sa_restorer);
        }  // @todo: 设置了SA_SIGINFO时restorer发生改变
        else {
            ctx.ra() = proc::vDSOfuncAddr(sigreturn);
        }  // @todo: VSDO
        ctx.sp() = ustream.addr();
        ctx.x(REG_A0) = signum;
        if(sa->sa_flags & SA_SIGINFO) {
            ctx.x(REG_A1)=pinfo;
            ctx.x(REG_A2)=puctx;
        }
        ctx.pc = reinterpret_cast<long>(sa->sa_handler);
        if(sa->sa_flags & SA_RESETHAND) {
            if(curproc->sigacts[signum-1] != nullptr) { curproc->sigacts[signum-1].reset(); }
        }
    }
} // namespace signal
