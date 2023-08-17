#include "lock.hh"
#include "kernel.hh"
#include "time.hh"
#include <EASTL/chrono.h>

namespace semaphore
{
    void Semaphore::req(){
        while(count==0){
            waiting.push_back(kHartObj().curtask);
            kernel::sleep();
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
    void condition_variable::notify_specific(proc::Task* task){
        waiting.remove(task);
        kGlobObjs->scheduler->wakeup(task);
    }
    void condition_variable::notify_all(){
        for(auto task:waiting){
            kGlobObjs->scheduler->wakeup(task);
        }
        waiting.clear();
    }
    void condition_variable::wait(){
        waiting.push_back(kHartObj().curtask);
        kernel::sleep();
    }
    bool condition_variable::wait_for(const eastl::chrono::nanoseconds& dura){
        auto curtask=kHartObj().curtask;
        kHartObj().timer->setTimeout(dura,[=](){ this->notify_specific(curtask); });
        wait();
        return true;
    }
} // namespace condition_variable

