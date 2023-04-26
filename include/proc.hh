#ifndef PROC_HH__
#define PROC_HH__
#include "common.h"
#include "sched.hh"
#include "vm.hh"

namespace proc
{
    using vm::PageTable;
    using sched::tid_t;
    using sched::Scheduable;
    using sched::prior_t;
    using vm::PageTable;

    struct Context
    {
        xlen_t gpr[30];
        constexpr xlen_t& x(int r){return gpr[r-1];}
        ptr_t stack;
        xlen_t satp;
        xlen_t pc;
    };

    struct Task;
    constexpr xlen_t UserStack=0xffffffff;

    struct Process:public Scheduable{
        tid_t parent;
        PageTable pagetable;
        klib::list<Task*> tasks;
        Process(tid_t pid,prior_t prior,tid_t parent,vm::pgtbl_t pgtbl,Task *init);
        inline Task* defaultTask(){ return tasks.head->data; }
    };
    struct Task:public Scheduable{ // a.k.a. kthread
        Context ctx;
        tid_t proc;
        Process *getProcess();
        Task(tid_t tid,prior_t pri,tid_t proc,xlen_t stack):Scheduable(tid,pri),proc(proc){
            ctx.gpr[1]=stack; //x2, sp
        }
        void switchTo();
    };
    Process* createProcess();
} // namespace proc

#endif