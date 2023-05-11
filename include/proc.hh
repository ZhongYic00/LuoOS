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
        xlen_t pc;
    };
    struct KContext:public Context{
        ptr_t kstack;
        xlen_t satp;
    };

    struct Task;
    constexpr xlen_t UserStack=0x7fffffff;

    struct Process:public Scheduable{
        using File=fs::File;
        tid_t parent;
        PageTable pagetable;
        klib::list<Task*> tasks;
        File* files[3];
        Process(tid_t pid,prior_t prior,tid_t parent);
        inline Task* defaultTask(){ return tasks.head->data; }
        inline tid_t pid(){return id;}
        inline prior_t priority(){return prior;}
        inline xlen_t satp(){return vm::PageTable::toSATP(pagetable);}
        inline File *ofile(int fd){return files[fd];}
        Task* newTask();
        void print();
    private:
        xlen_t newUstack();
        xlen_t newKstack();
        inline void addTask(Task* task){ tasks.push_back(task); }
    };
    struct Task:public Scheduable{ // a.k.a. kthread
        enum class Priv:bool{
            User,Kernel
        };
        Context ctx;
        KContext kctx;
        const tid_t proc;
        Priv lastpriv;
        Process *getProcess();
        inline Task(tid_t tid,prior_t pri,tid_t proc,xlen_t stack):Scheduable(tid,pri),proc(proc){
            ctx.x(2)=stack; //x2, sp
            kctx.satp=getProcess()->satp();
        }
        void switchTo();
        void sleep();
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
        Process *alloc(prior_t prior,tid_t prnt);
        void free(Process*);
        inline Process* operator[](tid_t pid){return proclist[pid];}
    };
    Process* createProcess();
    Process* createKProcess();
    void clone(Task* task);
} // namespace proc

#endif