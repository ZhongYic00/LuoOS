#ifndef PROC_HH__
#define PROC_HH__
#include "common.h"
#include "sched.hh"
#include "vm.hh"
#include "fs.hh"

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
        constexpr inline xlen_t& x(int r){return gpr[r-1];}
        inline xlen_t& sp(){return x(2);}
        ptr_t kstack;
        xlen_t satp;
        xlen_t pc;
    };

    struct Task;
    constexpr xlen_t UserStack=0x7fffffff;

    struct Process:public Scheduable{
        using File=fs::File;
        tid_t parent;
        PageTable pagetable;
        klib::list<Task*> tasks;
        File* files[3];
        Process(tid_t pid,prior_t prior,tid_t parent,vm::pgtbl_t pgtbl);
        inline Task* defaultTask(){ return tasks.head->data; }
        inline tid_t pid(){return id;}
        inline xlen_t satp(){return vm::PageTable::toSATP(pagetable);}
        inline File *ofile(int fd){return files[fd];}
        Task* newTask();
    private:
        xlen_t newUstack();
        xlen_t newKstack();
        inline void addTask(Task* task){ tasks.push_back(task); }
    };
    struct Task:public Scheduable{ // a.k.a. kthread
        Context ctx;
        const tid_t proc;
        Process *getProcess();
        inline Task(tid_t tid,prior_t pri,tid_t proc,xlen_t stack):Scheduable(tid,pri),proc(proc){
            ctx.x(2)=stack; //x2, sp
            ctx.satp=getProcess()->satp();
        }
        void switchTo();
    };
    class TaskManager{
        constexpr static int ntasks=128;
        int tidCnt;
        Task *tasklist[ntasks];
    public:
        Task* alloc(prior_t prior,tid_t prnt,xlen_t stack);
        void free(Task*);
    };
    class ProcManager{
        constexpr static int nprocs=128;
        int pidCnt;
        Process *proclist[nprocs];
    public:
        Process *alloc(prior_t prior,tid_t prnt,vm::pgtbl_t pgtbl);
        void free(Process*);
        inline Process* operator[](tid_t pid){return proclist[pid];}
    };
    Process* createProcess();
} // namespace proc

#endif