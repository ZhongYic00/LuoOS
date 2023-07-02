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
    using eastl::shared_ptr;
    using eastl::make_shared;
    using fs::File;
    using fs::DirEnt;

    struct Context
    {
        xlen_t gpr[30];
        constexpr inline xlen_t& x(int r){return gpr[r-1];}
        inline xlen_t& ra(){return x(1);}
        inline xlen_t& a0(){return x(10);}
        inline xlen_t& sp(){return x(2);}
        inline xlen_t& tp(){return x(4);}
        xlen_t pc;
        inline klib::string toString() const{
            klib::string rt;
            for(int i=1;i<31;i++)
                rt+=klib::format("%x,",gpr[i-1]);
            return klib::format("{gpr={%s},pc=%x}",rt.c_str(),pc);
        }
    };
    struct KContext:public Context{
        ptr_t kstack;
        xlen_t satp;
    };

    struct Task; 
    class Tms {
        private:
            long m_tms_utime;
            long m_tms_stime;
            long m_tms_cutime;
            long m_tms_cstime;
        public:
            Tms(): m_tms_utime(0), m_tms_stime(0), m_tms_cutime(0), m_tms_cstime(0) {}
            Tms(const Tms &a_tms): m_tms_utime(a_tms.m_tms_utime), m_tms_stime(a_tms.m_tms_stime), m_tms_cutime(a_tms.m_tms_cutime), m_tms_cstime(a_tms.m_tms_cstime) {}
            const Tms& operator=(const Tms &a_tms) {
                m_tms_utime = a_tms.m_tms_utime;
                m_tms_stime = a_tms.m_tms_stime;
                m_tms_cutime = a_tms.m_tms_cutime;
                m_tms_cstime = a_tms.m_tms_cstime;
                return *this;
            }
            const Tms& operator+=(const Tms &a_tms) {
                m_tms_cutime += a_tms.m_tms_utime;
                m_tms_cstime += a_tms.m_tms_stime;
                return *this;
            }
            inline void uTick() { ++m_tms_utime; }
            inline void sTick() { ++m_tms_stime; }
    };
    constexpr xlen_t UserStackDefault=0x7ffffff0,
        TrapframePages=2,
        UserHeapTop=(UserStackDefault-(1l<<29)),
        UserHeapBottom=vm::ceil(UserHeapTop-(1l<<30));
    constexpr int MaxOpenFile = 101; // 官网测例往fd=100中写东西

    typedef tid_t pid_t;
    struct Process:public IdManagable,public Scheduable{
        tid_t parent;
        VMAR vmar;
        xlen_t heapTop=UserHeapBottom;
        tinystl::unordered_set<Task*> tasks;
        shared_ptr<File> files[MaxOpenFile];
        tinystl::string name;
        Tms ti;
        DirEnt *cwd; // @todo 也许可以去掉，固定在fd = 3处打开工作目录
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
        inline shared_ptr<File> ofile(int fd){return files[fd];}
        Task* newTask();
        Task* newTask(const Task &other,bool allocStack=true);
        Task* newKTask(prior_t prior=0);
        void print();
        int fdAlloc(shared_ptr<File> a_file, int a_fd=-1);
        /// @todo rtval
        inline xlen_t brk(xlen_t addr){
            Log(info,"brk %lx",addr);
            if(addr>=UserHeapTop||addr<=UserHeapBottom)return heapTop;
            if(vmar.contains(addr))return heapTop=addr;
            else {
                /// @todo free redundant vmar
                /// @brief alloc new vmar
                using namespace vm;
                xlen_t curtop=bytes2pages(heapTop),
                    destop=bytes2pages(addr),
                    pages=destop-curtop;
                Log(info,"curtop=%x,destop=%x, needs to alloc %d pages",curtop,destop,pages);
                auto vmo=VMO::alloc(pages);
                using perm=PageTableEntry::fieldMasks;
                vmar.map(PageMapping{curtop,vmo,perm::r|perm::w|perm::x|perm::u|perm::v,PageMapping::MappingType::anon});
                return heapTop=addr;
            }
        }
        void exit(int status);
        void zombieExit();
    private:
        xlen_t newUstack();
        xlen_t newTrapframe();
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
        inline Task(tid_t tid,prior_t pri,tid_t proc):IdManagable(tid),Scheduable(pri),proc(proc),lastpriv(Priv::User){
            ctx.x(2)=UserStackDefault; //x2, sp
            kctx.satp=getProcess()->satp();
            kctx.kstack=reinterpret_cast<ptr_t>(this)+TrapframePages*vm::pageSize;
        }
        inline Task(prior_t pri,tid_t proc):Task(id,pri,proc){}
        inline Task(tid_t tid,const Task &other,pid_t proc):IdManagable(tid),Scheduable(other.prior),proc(proc),lastpriv(Priv::User){
            ctx=other.ctx;
            kctx=other.kctx;
            kctx.satp=getProcess()->satp();
            kctx.kstack=reinterpret_cast<ptr_t>(this)+TrapframePages*vm::pageSize;
        }
        inline Task(const Task &other,pid_t proc):Task(id,other,proc){}

        void switchTo();
        void sleep();

        inline klib::string toString(bool detail=false) const{
            if(detail)return klib::format("Task<%d> priv=%d kctx=%s",id,lastpriv,kctx.toString());
            else return klib::format("Task<%d> priv=%d sp=%x ksp=%x pc=%x kra=%x",id,lastpriv,ctx.gpr[1],kctx.gpr[1],ctx.pc,kctx.gpr[0]);
        }

        template<typename ...Ts>
        static Task* createTask(ObjManager<Task> &mgr,xlen_t buff,Ts&& ...args){
            /**
             * @brief mem layout. low -> high
             * | task struct | kstack |
             */
            Task* task=reinterpret_cast<Task*>(buff);
            auto id=mgr.newId();
            new (task) Task(id,args...);
            mgr.addObj(id,task);
            return task;
        }
        void operator delete(ptr_t task){}
    };
    struct SleepingTask {
        struct Task *m_task;
        long m_wakeup_tick;
        SleepingTask():m_task(nullptr), m_wakeup_tick(0) {}
        SleepingTask(struct Task *a_task, uint a_tick):m_task(a_task), m_wakeup_tick(a_tick) {}
    };
    struct KTask:public Task{};
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