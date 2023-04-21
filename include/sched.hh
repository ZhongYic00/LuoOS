#ifndef SCHED_HH__
#define SCHED_HH__
#include "klib.hh"
#include "kernel.hh"

namespace sched
{
    enum State:short{
        Init,Ready,Running,Pending,Exit,
    };
    typedef int tid_t;
    typedef short prior_t;
    struct Scheduable{
        tid_t id;
        prior_t prior;
        State state;
        Scheduable(tid_t id,prior_t prior):id(id),prior(prior),state(Init){}
    };
    using vm::PageTable;
    struct Task;
    constexpr xlen_t UserStack=0xffffffff;
    struct Process:public Scheduable{
        tid_t parent;
        PageTable pagetable;
        klib::list<Task*> tasks;
        Process(tid_t pid,prior_t prior,tid_t parent,vm::pgtbl_t pgtbl,Task *init):Scheduable(pid,prior),parent(parent),pagetable(pgtbl){
            tasks.push_back(init);
            kernel::createKernelMapping(pagetable);
            using perm=vm::PageTableEntry::fieldMasks;
            pagetable.createMapping(vm::addr2pn(UserStack),kernelPmgr->alloc(1),1,perm::r|perm::w|perm::u|perm::v);
            pagetable.print();
        }
        inline Task* defaultTask(){ return tasks.head->data; }
    };
    using kernel::Context;
    struct Task:public Scheduable{ // a.k.a. kthread
        Context ctx;
        tid_t proc;
        Process *getProcess();
        Task(tid_t tid,prior_t pri,tid_t proc,xlen_t stack):Scheduable(tid,pri),proc(proc){
            ctx.gpr[1]=stack; //x2, sp
        }
    };
    Process* createProcess();
} // namespace sched


#endif