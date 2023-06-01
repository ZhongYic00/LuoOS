#ifndef IPC_HH__
#define IPC_HH__
#include "common.h"
#include "klib.hh"
#include "lock.hh"
// #include "proc.hh"
namespace proc{
    struct Task;
}

namespace pipe{
    using proc::Task;
    struct Pipe{
        klib::ringbuf<uint8_t> buff;
        semaphore::Semaphore wsem,rsem;
        klib::list<Task*> waiting;
        Pipe():wsem(64),rsem(0){}
        inline void write(klib::ByteArray bytes){
            auto n=bytes.len;
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
            for(auto i=bytes.begin();!buff.empty()&&n;n--,i++){
                rsem.req();
                *i=buff.get();buff.pop();
                wsem.rel();
            }
            // wakeup();
            return bytes;
        }
        void wakeup();
        void sleep();
    };
}
#endif