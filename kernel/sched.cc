#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace sched;

void schedule(){
    printf("scheduling\n");
    auto curtask=static_cast<proc::Task*>(kGlobObjs.scheduler.next());
    printf("current task=%d proc=[%d]%s\n",curtask->id,curtask->getProcess()->pid(),curtask->getProcess()->name.c_str());
    curtask->switchTo();
}
Scheduable* sched::Scheduler::next(){
    return *(++cur);
}
void sched::Scheduler::add(Scheduable *task){
    ready.push_back(task);
    cur=ready.begin();
}
Scheduler::Scheduler():cur(ready.begin()){
}
void Scheduler::sleep(Scheduable *task){
    ready.remove(task);
    pending.push_back(task);
}