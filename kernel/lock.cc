#include "lock.hh"
#include "kernel.hh"

namespace semaphore
{
    void Semaphore::req(){
        while(count==0){
            waiting.push_back(kHartObj().curtask);
            syscall::sleep();
        }
        count--;
    }
    void Semaphore::rel(){
        count++;
        if(!waiting.empty()){
            auto front=waiting.pop_front();
            Log(info,"wakeup%s",front->toString().c_str());
            kGlobObjs->scheduler->wakeup(front);
        }
    }
} // namespace semaphore
