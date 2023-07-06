#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

// #define moduleLevel LogLevel::debug

using namespace sched;

void schedule(){
    Log(info,"scheduling");
    auto curtask=static_cast<proc::Task*>(kGlobObjs->scheduler->next(kHartObj().curtask));
    Log(info,"current task=%d proc=[%d]%s",curtask->id,curtask->getProcess()->pid(),curtask->getProcess()->name.c_str());
    curtask->switchTo();
    assert(kHartObjs[0].curtask!=kHartObjs[1].curtask);
}
Scheduable* sched::Scheduler::next(Scheduable* prev){
    if(prev)add(prev);
    for(int i=0;i<=maxPrior;i++){
        if(!ready[i].empty()){
            auto rt=ready[i].front();
            ready[i].pop_front();
            return rt;
        }
    }
}
klib::string print(Scheduable* const &tsk){
    auto task=static_cast<const proc::Task*>(tsk);
    return task->toString();
}
void sched::Scheduler::add(Scheduable *task){
    // Log(debug,"add(%s)",static_cast<proc::Task*>(task)->toString().c_str());
    if(task->state==Zombie||task->state==Pending)
        return ;
    auto &ready=this->ready[task->prior];
    // auto &cur=this->cur[task->prior];
    ready.push_back(task);
    if(kHartObj().curtask && kHartObj().curtask->prior>task->prior){
        // issue an immediate schedule
        csrSet(sip,BIT(csr::mip::stip));
    }
    // Log(debug,"%s",ready.toString(print).c_str());
}
Scheduler::Scheduler(){}
void Scheduler::sleep(Scheduable *task){
    auto &ready=this->ready[task->prior];
    task->state=Pending;
    // Log(debug,"%s",ready.toString(print).c_str());
    ready.remove(task);
    // Log(debug,"%s",ready.toString(print).c_str());
    pending.push_back(task);
}
void Scheduler::wakeup(Scheduable *task){
    Log(info,"wakeup %s",static_cast<proc::Task*>(task)->toString().c_str());
    if(task->state!=Pending)return ;
    pending.remove(task);
    task->state=Ready;
    add(task);
}
void Scheduler::remove(Scheduable *task){
    auto &ready=this->ready[task->prior];
    pending.remove(task);
    ready.remove(task);
}