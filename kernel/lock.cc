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
            auto front=waiting.front();waiting.pop_front();
            Log(info,"wakeup%s",front->toString().c_str());
            kGlobObjs->scheduler->wakeup(front);
        }
    }
} // namespace semaphore

namespace condition_variable
{
    void condition_variable::notify_one(){
        if(waiting.empty())return ;
        kGlobObjs->scheduler->wakeup(waiting.front());
        waiting.pop_front();
    }
    void condition_variable::notify_all(){
        for(auto task:waiting){
            kGlobObjs->scheduler->wakeup(task);
        }
        waiting.clear();
    }
    void condition_variable::wait(){
        waiting.push_back(kHartObj().curtask);
        syscall::sleep();
    }
} // namespace condition_variable

