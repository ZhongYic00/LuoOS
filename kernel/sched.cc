#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

// #define moduleLevel LogLevel::debug

using namespace sched;

void schedule(){
    Log(info,"scheduling");
    auto curtask=static_cast<proc::Task*>(kGlobObjs.scheduler.next());
    Log(info,"current task=%d proc=[%d]%s",curtask->id,curtask->getProcess()->pid(),curtask->getProcess()->name.c_str());
    curtask->switchTo();
}
Scheduable* sched::Scheduler::next(){
    return *(cur++);
}
klib::string print(Scheduable* const &tsk){
    auto task=static_cast<const proc::Task*>(tsk);
    return task->toString();
}
void sched::Scheduler::add(Scheduable *task){
    if(ready.empty()){
        ready.push_back(task);
        cur=ready.begin();
    } else {
        ready.push_back(task);
    }
    Log(debug,"%s",ready.toString(print).c_str());
}
Scheduler::Scheduler():cur(ready.begin()){
}
void Scheduler::sleep(Scheduable *task){
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
    if(cur==ready.find(task))cur++;
    if(pending.find(task)!=pending.end())pending.remove(task);
    else if(ready.find(task)!=ready.end())ready.remove(task);
}