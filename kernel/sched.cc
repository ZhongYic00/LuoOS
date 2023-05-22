#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace sched;

void schedule(){
    Log(info,"scheduling");
    auto curtask=static_cast<proc::Task*>(kGlobObjs.scheduler.next());
    Log(info,"current task=%d proc=[%d]%s",curtask->id,curtask->getProcess()->pid(),curtask->getProcess()->name.c_str());
    curtask->switchTo();
}
Scheduable* sched::Scheduler::next(){
    return *(++cur);
}
klib::string print(Scheduable* const &tsk){
    auto task=static_cast<const proc::Task*>(tsk);
    return task->toString();
}
void sched::Scheduler::add(Scheduable *task){
    ready.push_back(task);
    Log(debug,"%s",ready.toString(print).c_str());
    cur=ready.begin();
}
Scheduler::Scheduler():cur(ready.begin()){
}
void Scheduler::sleep(Scheduable *task){
    Log(debug,"%s",ready.toString(print).c_str());
    ready.remove(task);
    Log(debug,"%s",ready.toString(print).c_str());
    pending.push_back(task);
    cur=ready.begin();
}
void Scheduler::wakeup(Scheduable *task){
    if(pending.find(task)==pending.end())return ;
    pending.remove(task);
    ready.push_back(task);
}