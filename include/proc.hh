#ifndef PROC_HH__
#define PROC_HH__
// #include "common.h"
// #include "resmgr.hh"
#include "sched.hh"
#include "vm.hh"
#include "TINYSTL/unordered_set.h"
#include "TINYSTL/string.h"
#include "TINYSTL/vector.h"
#include "fat.hh"

namespace proc
{
    using vm::PageTable;
    using sched::Scheduable;
    using sched::prior_t;
    using vm::VMAR;
    using klib::SharedPtr;

    struct Context
    {
        xlen_t gpr[30];
        constexpr inline xlen_t& x(int r){return gpr[r-1];}
        inline xlen_t& ra(){return x(1);}
        inline xlen_t& a0(){return x(10);}
        inline xlen_t& sp(){return x(2);}
        xlen_t pc;
    };
    struct KContext:public Context{
        ptr_t kstack;
        xlen_t satp;
    };

    struct Task;
    constexpr xlen_t UserStackDefault=0x7ffffff0;
    constexpr int MaxOpenFile = 101; // 官网测例往fd=100中写东西

    typedef tid_t pid_t;
    struct Process:public IdManagable,public Scheduable{
        using File=fs::File;
        using dirent=fs::dirent;
        using mapped_file=fs::mapped_file;
        tid_t parent;
        VMAR vmar;
        tinystl::unordered_set<Task*> tasks;
        SharedPtr<File> files[MaxOpenFile];
        tinystl::string name;
        // todo: 以下为临时的FAT接口，需要修改
        dirent *cwd;
        mapped_file mfile;    //映射的文件的范围
        int exitstatus;

        Process(prior_t prior,tid_t parent);
        Process(tid_t pid,prior_t prior,tid_t parent);
        Process(const Process &other,tid_t pid);
        inline Process(const Process &other):Process(other,id){}
        ~Process();

        inline Task* defaultTask(){ return *tasks.begin(); } // @todo needs to mark default
        Process *parentProc();
        inline tid_t pid(){return id;}
        inline prior_t priority(){return prior;}
        inline xlen_t satp(){return vmar.satp();}
        inline SharedPtr<File> ofile(int fd){return files[fd];}
        Task* newTask();
        Task* newTask(const Task &other,bool allocStack=true);
        Task* newKTask(prior_t prior=0);
        void print();
        int fdAlloc(SharedPtr<File> a_file, int a_fd=-1);
        void exit(int status);
        void zombieExit();
    private:
        xlen_t newUstack();
        xlen_t newKstack();
        inline void addTask(Task* task){ tasks.insert(task); }
    };
    struct Task:public IdManagable,public Scheduable{ // a.k.a. kthread
        enum class Priv:uint8_t{
            User,Kernel,AlwaysKernel
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

        inline klib::string toString() const{
            return klib::format("Task<%d>",id);
        }
    };
    struct KTask:public Task{
        inline KTask(tid_t tid,prior_t pri,tid_t proc,xlen_t stack):Task(tid,pri,proc,stack){lastpriv=Priv::AlwaysKernel;}
        inline KTask(prior_t pri,tid_t proc,xlen_t stack):KTask(id,pri,proc,stack){}
    };
    typedef ObjManager<Process> ProcManagerBase;
    typedef ObjManager<Task> TaskManager;
    class ProcManager:public ProcManagerBase{
    public:
        tinystl::vector<Process*> getChilds(pid_t pid){
            tinystl::vector<Process*> rt;
            for(int i=0;i<128;i++){
                auto p=this->operator[](i);
                if(p && p->parent==pid)
                    rt.push_back(p);
            }
            return rt;
        }
    };
    Process* createProcess();
    Process* createKProcess(prior_t prior);
    pid_t clone(Task* task);
} // namespace proc


#endif