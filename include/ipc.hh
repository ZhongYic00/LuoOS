#ifndef IPC_HH__
#define IPC_HH__
#include "common.h"
#include "klib.hh"
#include "lock.hh"
#include <EASTL/bitset.h>
#include <EASTL/unique_ptr.h>

namespace proc{
    struct Task;
    struct Process;
}

namespace pipe{
    using proc::Task;
    struct Pipe{
        klib::ringbuf<uint8_t> buff;
        semaphore::Semaphore wsem,rsem;
        klib::list<Task*> waiting;
        int wcnt,rcnt;
        Pipe():wsem(64),rsem(0),wcnt(0),rcnt(0){}
        inline void addReader(){rcnt++;}
        inline void decReader(){rcnt--;}
        inline void addWriter(){wcnt++;}
        inline void decWriter(){wcnt--;}
        inline void write(klib::ByteArray bytes){
            auto n=bytes.len;
            if(rcnt==0)return ;
            // TODO parallelize, copy avai bytes at once
            for(auto b:bytes){
                wsem.req();
                // while(buff.full()){
                //     Log(info,"Pipe::write blocked, %d bytes remains\n",n);
                //     //sleep and wakeup
                //     wakeup();
                //     sleep();
                // }
                buff.put(b);
                rsem.rel();
                n--;
            }
            // wakeup();
        }
        inline klib::ByteArray read(int n){
            klib::ByteArray bytes(n);
            // while(buff.empty())sleep();
            /// @todo error handling
            if(buff.empty()&&wcnt==0)return 0;
            for(auto i=bytes.begin();n;n--,i++){
                rsem.req();
                *i=buff.get();buff.pop();
                wsem.rel();
                if(buff.empty())break;
            }
            // wakeup();
            return bytes;
        }
        void wakeup();
        void sleep();
    };
}
namespace signal{

#define SA_RESTORER 1
    #include <asm-generic/siginfo.h>
    #include <asm/signal.h>

    using eastl::bitset;
    using eastl::unique_ptr;
    using proc::Process;
    using proc::Task;

    constexpr static int numSignals=32;
    typedef bitset<numSignals> SignalMask;
    typedef sigaction SignalAction;
    typedef siginfo_t SignalInfo;
    void send(Process &proc,int num,unique_ptr<SignalInfo>& info);
    void send(Task &task,int num,unique_ptr<SignalInfo>& info);
    inline SignalMask sigset2bitset(sigset_t set){return set.sig[0];}
}
#endif