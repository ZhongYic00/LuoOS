#ifndef IPC_HH__
#define IPC_HH__
#include "common.h"
#include "klib.hh"
// #include "proc.hh"
namespace proc{
    struct Task;
}

namespace pipe{
    using proc::Task;
    struct Pipe{
        klib::ringbuf<uint8_t> buff;
        klib::list<Task*> waiting;
        inline void write(klib::ByteArray bytes){
            // TODO parallelize, copy avai bytes at once
            for(auto b:bytes){
                while(buff.full()){
                    //sleep and wakeup
                    wakeup();
                    sleep();
                }
                buff.put(b);
            }
        }
        inline klib::ByteArray read(int n){
            klib::ByteArray bytes(n);
            while(buff.empty())sleep();
            for(auto i=bytes.begin();!buff.empty()&&n;n--,i++){
                *i=buff.get();buff.pop();
            }
            return bytes;
        }
        void wakeup();
        void sleep();
    };
}
#endif