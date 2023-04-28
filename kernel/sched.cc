#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace sched;

void schedule(){
    printf("scheduling\n");
    auto curtask=reinterpret_cast<proc::Task*>(kGlobObjs.scheduler.next());
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