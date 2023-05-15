#ifndef PROC_HH__
#define PROC_HH__
#include "common.h"
#include "sched.hh"
#include "vm.hh"
#include "fs.hh"
#include "resmgr.hh"

namespace proc
{
    using vm::PageTable;
    using sched::Scheduable;
    using sched::prior_t;
    using vm::VMAR;

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
    constexpr xlen_t UserStackDefault=0x7fffffff;
    constexpr int MaxOpenFile = 3;

    typedef tid_t pid_t;
    struct Process:public IdManagable,public Scheduable{
        using File=fs::File;
        tid_t parent;
        VMAR vmar;
        klib::list<Task*> tasks;
        File* files[MaxOpenFile];

        Process(prior_t prior,tid_t parent);
        Process(tid_t pid,prior_t prior,tid_t parent);
        Process(const Process &other,tid_t pid);
        inline Process(const Process &other):Process(other,id){}
        inline Task* defaultTask(){ return tasks.head->data; }
        inline tid_t pid(){return id;}
        inline prior_t priority(){return prior;}
        inline xlen_t satp(){return vmar.satp();}
        inline File *ofile(int fd){return files[fd];}
        Task* newTask();
        Task* newTask(const Task &other);
        void print();
        int fdAlloc(File *file, int newfd=-1);
    private:
        xlen_t newUstack();
        xlen_t newKstack();
        inline void addTask(Task* task){ tasks.push_back(task); }
    };
    struct Task:public IdManagable,public Scheduable{ // a.k.a. kthread
        enum class Priv:bool{
            User,Kernel
        };
        Context ctx;
        KContext kctx;
        const pid_t proc;
        Priv lastpriv;
        Process *getProcess();
        inline Task(tid_t tid,prior_t pri,tid_t proc,xlen_t stack):IdManagable(tid),Scheduable(pri),proc(proc),lastpriv(Priv::User){
            ctx.x(2)=stack; //x2, sp
            kctx.satp=getProcess()->satp();
        }
        inline Task(prior_t pri,tid_t proc,xlen_t stack):Task(id,pri,proc,stack){}
        inline Task(const Task &other,tid_t tid,pid_t proc):IdManagable(tid),Scheduable(other.prior),proc(proc),lastpriv(Priv::User){
            ctx=other.ctx;
            kctx=other.kctx;
            kctx.satp=getProcess()->satp();
        }
        inline Task(const Task &other,pid_t proc):Task(other,id,proc){}
        inline Task(const Task &other,tid_t tid,pid_t proc,ptr_t kstack):IdManagable(tid),Scheduable(other.prior),proc(proc),lastpriv(Priv::User){
            kctx.kstack=kstack;
        }
        void switchTo();
        void sleep();
    };
    typedef ObjManager<Process> ProcManager;
    typedef ObjManager<Task> TaskManager;
    Process* createProcess();
    Process* createKProcess();
    void clone(Task* task);
} // namespace proc


#endif