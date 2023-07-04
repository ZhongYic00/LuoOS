#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

// #define moduleLevel LogLevel::info

using namespace sched;

void schedule(){
    Log(info,"scheduling");
    auto curtask=static_cast<proc::Task*>(kGlobObjs->scheduler->next());
    Log(info,"current task=%d proc=[%d]%s",curtask->id,curtask->getProcess()->pid(),curtask->getProcess()->name.c_str());
    curtask->switchTo();
}
Scheduable* sched::Scheduler::next(){
    for(int i=0;i<=maxPrior;i++){
        if(!ready[i].empty())return *(cur[i]++);
    }
}
klib::string print(Scheduable* const &tsk){
    auto task=static_cast<const proc::Task*>(tsk);
    return task->toString();
}
void sched::Scheduler::add(Scheduable *task){
    auto &ready=this->ready[task->prior];
    auto &cur=this->cur[task->prior];
    if(ready.empty()){
        ready.push_back(task);
        cur=ready.begin();
        if(kHartObj().curtask && kHartObj().curtask->prior>task->prior){
            // issue an immediate schedule
            csrSet(sip,BIT(csr::mip::stip));
        }
    } else {
        ready.push_back(task);
    }
    Log(debug,"%s",ready.toString(print).c_str());
}
Scheduler::Scheduler():cur({ready[0].begin(),ready[1].begin()}){
}
void Scheduler::sleep(Scheduable *task){
    auto &ready=this->ready[task->prior];
    auto &cur=this->cur[task->prior];
    Log(debug,"%s",ready.toString(print).c_str());
    if(cur==ready.find(task))cur++;
    ready.remove(task);
    Log(debug,"%s",ready.toString(print).c_str());
    pending.push_back(task);
}
void Scheduler::wakeup(Scheduable *task){
    Log(info,"wakeup %s",static_cast<proc::Task*>(task)->toString().c_str());
    if(pending.find(task)==pending.end())return ;
    pending.remove(task);
    add(task);
}
void Scheduler::remove(Scheduable *task){
    auto &ready=this->ready[task->prior];
    auto &cur=this->cur[task->prior];
    if(cur==ready.find(task))cur++;
    if(pending.find(task)!=pending.end())pending.remove(task);
    else if(ready.find(task)!=ready.end())ready.remove(task);
}