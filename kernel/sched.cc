#include "sched.hh"
#include "proc.hh"
#include "rvcsr.hh"
#include "kernel.hh"

#define moduleLevel LogLevel::debug

using namespace sched;

extern void _strapexit();
// FORCEDINLINE
// extern "C" void contextSave();
// FORCEDINLINE
// extern "C" void contextRestore();
void schedule(){
    Log(info,"scheduling");
    auto prvtask=kHartObj().curtask;
    auto curtask=static_cast<proc::Task*>(kGlobObjs->scheduler->next(kHartObj().curtask));
    Log(info,"prevtask=%x current task=%d proc=[%d]%s",prvtask,curtask->id,curtask->getProcess()->pid(),curtask->getProcess()->name.c_str());
    curtask->switchTo();
    // clock tick
    curtask->getProcess()->stats.ti.tms_utime++;
    assert(kHartObjs[0].curtask!=kHartObjs[1].curtask);
    // save context
    // if(prvtask){
    saveContextTo(prvtask->kctx.gpr);
    // Log(info,"prvtask=%s",prvtask->toString(true).c_str());
    // }
    if(curtask->lastpriv==proc::Task::Priv::User)
        _strapexit();
    else {
        // restore context
        Log(debug,"restore kctx");
        restoreContextFrom(curtask->kctx.gpr);
    }
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
    return nullptr;
}
string print(Scheduable* const &tsk){
    auto task=static_cast<const proc::Task*>(tsk);
    return task->toString();
}
void sched::Scheduler::add(Scheduable *task){
    Log(debug,"add(%s)",static_cast<proc::Task*>(task)->toString().c_str());
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
    assert(task->state!=Pending);
    task->state=Pending;
    // Log(debug,"%s",ready.toString(print).c_str());
    ready.remove(task);
    // Log(debug,"%s",ready.toString(print).c_str());
    pending.insert(task);
}
void Scheduler::wakeup(Scheduable *task){
    Log(info,"wakeup %s",static_cast<proc::Task*>(task)->toString().c_str());
    if(task->state!=Pending)return ;
    pending.erase(task);
    task->state=Ready;
    add(task);
}
void Scheduler::remove(Scheduable *task){
    auto &ready=this->ready[task->prior];
    pending.insert(task);
    ready.remove(task);
}