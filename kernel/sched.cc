#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

using namespace sched;

void schedule(){
    printf("scheduling\n");
    auto cur=kGlobObjs.scheduler.next();
    reinterpret_cast<proc::Task*>(cur)->switchTo();
}
Scheduable* sched::Scheduler::next(){
    if(cur==pool.tail||cur==nullptr)cur=pool.head;
    else cur=cur->iter.next;
    return cur->data;
}
void sched::Scheduler::add(Scheduable *task){
    pool.push_back(task);
}