#ifndef IPC_HH__
#define IPC_HH__
#include "common.h"
// #include "proc.hh"
namespace proc{
    struct Process;
}

extern int sleep();
namespace pipe{
    using proc::Process;
    struct Pipe{
        klib::ringbuf<uint8_t> buff;
        klib::list<Process*> readers;
        Process* writer;
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
        inline klib::ByteArray read(){
            klib::ByteArray buff(0);
            return buff;
        }
        inline void wakeup(){
            // for(auto reader:readers){
            //     // kGlobObjs.scheduler.wakeup(reader);
            // }
        }
    };
}
#endif