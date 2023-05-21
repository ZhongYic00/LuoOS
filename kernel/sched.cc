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
void print(Scheduable* const &tsk){
    auto task=static_cast<const proc::Task*>(tsk);
    printf("Task<%d>, ",task->id);
}
void sched::Scheduler::add(Scheduable *task){
    ready.push_back(task);
    ready.print(print);
    cur=ready.begin();
}
Scheduler::Scheduler():cur(ready.begin()){
}
void Scheduler::sleep(Scheduable *task){
    ready.print(print);
    ready.remove(task);
    ready.print(print);
    pending.push_back(task);
    cur=ready.begin();
}
void Scheduler::wakeup(Scheduable *task){
    if(pending.find(task)==pending.end())return ;
    pending.remove(task);
    ready.push_back(task);
}