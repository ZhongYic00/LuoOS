#ifndef IPC_HH__
#define IPC_HH__
#include "common.h"
#include "klib.hh"
#include "lock.hh"
// #include <EASTL/bitset.h>

namespace proc{
    struct Task;
    struct Process;
    struct Context;
}

namespace pipe{
    using proc::Task;
    struct Pipe{
        klib::ringbuf<uint8_t> buff;
        static const size_t bufSize=64;
        uint8_t buf[bufSize];
        xlen_t head,tail;
        condition_variable::condition_variable canr,canw;
        int wcnt,rcnt;
        Pipe():head(0),tail(0),wcnt(0),rcnt(0){}
        inline void addReader(){rcnt++;}
        inline void decReader(){rcnt--; if(rcnt==0)canw.notify_all();}
        inline void addWriter(){wcnt++;}
        inline void decWriter(){wcnt--; if(wcnt==0)canr.notify_all();}
        bool full(){return tail==head+bufSize;}
        bool empty(){return head==tail;}
        bool writable(){return !full()||rcnt;}
        bool readable(){return !empty()||wcnt;}
        inline void tryWakeup(){
            if(!empty())canr.notify_one();
            if(!full())canw.notify_one();
        }
        inline int write(ByteArray bytes){
            int off=0;
            for(;off<bytes.len;){
                auto wbegin=buf+tail%bufSize;
                auto wlen=klib::min(klib::min(head+bufSize-tail,bufSize-tail%bufSize),bytes.len-off);
                assert(wlen>=0);
                if(wlen==0){
                    if(rcnt==0) break;
                    canr.notify_one();
                    canw.wait();
                    continue;
                }
                memmove(wbegin,bytes.buff+off,wlen);
                tail+=wlen;
                off+=wlen;
            }
            tryWakeup();
            return off;
        }
        inline int read(ByteArray bytes){
            /// @todo error handling
            int off=0;
            for(;off<bytes.len;){
                auto rbegin=buf+head%bufSize;
                auto rlen=klib::min(klib::min(tail-head,bufSize-head%bufSize),bytes.len-off);
                if(rlen==0){
                    if(wcnt==0 || off>0) break;
                    canw.notify_one();
                    canr.wait();
                    continue;
                }
                memmove(bytes.buff+off,rbegin,rlen);
                head+=rlen;
                off+=rlen;
            }
            tryWakeup();
            return off;
        }
    };
}
namespace signal {

    #define SA_RESTORER 1
    #include <asm-generic/siginfo.h>
    #include <asm/signal.h>
    #include <asm/sigcontext.h>
    // #include <asm/ucontext.h>
    // 原声明过不了编译
    struct ucontext {
        unsigned long	  uc_flags;
        struct ucontext	 *uc_link;
        stack_t		  uc_stack;
        sigset_t	  uc_sigmask;
        /* There's some padding here to allow sigset_t to be expanded in the
        * future.  Though this is unlikely, other architectures put uc_sigmask
        * at the end of this structure and explicitly state it can be
        * expanded, so we didn't want to box ourselves in here. */
        __u8		  _unused[1024 / 8 - sizeof(sigset_t)];  // __unused过不了编译
        /* We can't put uc_sigmask at the end of this structure because we need
        * to be able to expand sigcontext in the future.  For example, the
        * vector ISA extension will almost certainly add ISA state.  We want
        * to ensure all user-visible ISA state can be saved and restored via a
        * ucontext, so we're putting this at the end in order to allow for
        * infinite extensibility.  Since we know this will be extended and we
        * assume sigset_t won't be extended an extreme amount, we're
        * prioritizing this. */
        struct sigcontext uc_mcontext;
    };

    // using eastl::bitset;
    using proc::Process;
    using proc::Task;
    using proc::Context;

    // constexpr int REG_PC = 0
    constexpr int REG_RA = 1;
    constexpr int REG_SP = 2;
    constexpr int REG_TP = 4;
    constexpr int REG_S0 = 8;
    constexpr int REG_S1 = 9;
    constexpr int REG_A0 = 10;
    constexpr int REG_A1 = 11;
    constexpr int REG_A2 = 12;
    constexpr int REG_S2 = 18;
    constexpr int REG_NARGS = 8;
    // constexpr int sigStackSiz = 2 * vm::pageSize;
    constexpr int sigStackSiz = 1UL << 13;  // 8192
    constexpr int numSigs = _NSIG;

    // constexpr static int numSigs=32;  // @todo: 检查一下是不是写错了
    // typedef bitset<numSigs> SigMask;
    typedef xlen_t SigMask;
    typedef sigaction SigAct;
    typedef sigset_t SigSet;
    typedef siginfo_t SigInfo;
    typedef stack_t SigStack;
    typedef sigcontext SigCtx;
    typedef ucontext UCtx;

    void sigInit();
    void sigSend(Process &proc,int a_sig, shared_ptr<SigInfo> a_info = nullptr);
    void sigSend(Task &task,int a_sig, shared_ptr<SigInfo> a_info = nullptr);
    // inline SigMask sigset2bitset(SigSet set){return set.sig[0];}
    inline bool sigMaskBit(SigMask a_mask, int a_bit) { return a_mask & (1UL<<a_bit); }
    inline void sigMaskBitSet(SigMask &a_mask, int a_bit, bool a_set) { a_set ? a_mask|=(1UL<<a_bit) : a_mask&=~(1UL<<a_bit); }
    xlen_t sigAction(int a_sig, SigAct *a_act, SigAct *a_oact);
    xlen_t sigProcMask(int a_how, SigSet *a_nset, SigSet *a_oset, size_t a_sigsetsize);
    xlen_t sigReturn();
    void sigHandler();
}
extern shared_ptr<signal::SigAct> defaultSigAct;
#endif