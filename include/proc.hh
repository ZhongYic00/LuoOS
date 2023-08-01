#ifndef PROC_HH__
#define PROC_HH__
// #include "common.h"
// #include "resmgr.hh"
#include "sched.hh"
#include "vm.hh"
#include "fs.hh"
#include "resource.hh"

namespace proc
{
    using vm::PageTable;
    using sched::Scheduable;
    using sched::prior_t;
    using vm::VMAR;
    using fs::File;
    // using fs::DirEnt;
    using fs::DEntry;
    using resource::RLim;
    using resource::RSrc;
    using namespace signal;

    struct Context
    {
        xlen_t gpr[31];
        FORCEDINLINE inline xlen_t& x(int r){return gpr[r-1];}
        FORCEDINLINE inline xlen_t& ra(){return x(1);}
        FORCEDINLINE inline xlen_t& a0(){return x(10);}
        FORCEDINLINE inline xlen_t& sp(){return x(2);}
        FORCEDINLINE inline xlen_t& tp(){return x(4);}
        xlen_t pc;
        inline string toString() const{
            string rt;
            for(int i=1;i<31;i++)
                rt+=klib::format("%x,",gpr[i-1]);
            return klib::format("{gpr={%s},pc=%x}",rt.c_str(),pc);
        }
    };
    struct KContext:public Context{
        xlen_t satp;
        xlen_t vaddr;
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
    constexpr xlen_t UserStackDefault=0x8000000,
        UstackSize=0x10000,
        UstackBottom=UserStackDefault-UstackSize,
        TrapframePages=2,
        UserHeapSize=0x100000;
    constexpr addr_t vDSOBase=0x9000000,
        vDSOPages=1,
        /// @note may not be fixed
        interpreterBase=0x70000000;
    template<typename fptr_t>
    constexpr inline addr_t vDSOfuncAddr(fptr_t func){return vDSOBase+(addr_t)func&vm::vaddrOffsetMask;}
    constexpr int mOFiles = 101; // 官网测例往fd=100中写东西
    constexpr int FdCwd = 3;

    struct Process:public IdManagable,public Scheduable{

        pid_t parent;
        VMAR vmar;
        xlen_t heapTop,heapBottom;
        unordered_set<Task*> tasks;
        shared_ptr<File> files[mOFiles];
        string name;
        string exe;
        Tms ti;
        shared_ptr<DEntry> cwd; // @todo 也许可以去掉，固定在fd = 3处打开工作目录
        int exitstatus;
        shared_ptr<SigAct> sigacts[numSigs];
        pid_t m_pgid, m_sid;
        uid_t m_ruid, m_euid, m_suid;
        gid_t m_rgid, m_egid, m_sgid;
        unordered_set<gid_t> supgids;
        mode_t umask;
        RLim rlimits[RSrc::RLIMIT_NLIMITS];

        Process(prior_t prior,pid_t parent);
        Process(pid_t pid,prior_t prior,pid_t parent);
        Process(const Process &other,pid_t pid);
        inline Process(const Process &other):Process(other,id){}
        ~Process();

        inline Task* defaultTask(){ return *tasks.begin(); } // @todo needs to mark default
        Process *parentProc();
        inline pid_t pid() { return id; }
        // 下列id读/写一体
        inline pid_t& pgid() { return m_pgid; }
        inline pid_t& sid() { return m_sid; }
        inline uid_t& ruid() { return m_ruid; }
        inline uid_t& euid() { return m_euid; }
        inline uid_t& suid() { return m_suid; }
        inline gid_t& rgid() { return m_rgid; }
        inline gid_t& egid() { return m_egid; }
        inline gid_t& sgid() { return m_sgid; }
        inline void clearGroups() { supgids.clear(); }
        inline int getGroupsNum() { return supgids.size(); }
        ByteArray getGroups(int a_size);
        void setGroups(ArrayBuff<gid_t> a_grps);
        int setUMask(mode_t a_mask);
        inline ByteArray getRLimit(int a_rsrc) { return ByteArray((uint8*)(rlimits+a_rsrc), sizeof(rlimits)); }
        int setRLimit(int a_rsrc, const RLim *a_rlim);
        inline shared_ptr<SigAct> getSigAct(int a_sig) { return sigacts[a_sig]==nullptr ? defaultSigAct : sigacts[a_sig]; }
        inline prior_t priority(){return prior;}
        inline xlen_t satp(){return vmar.satp();}
        shared_ptr<File> ofile(int a_fd);  // 要求a_fd所指文件存在时，可以直接使用该函数打开，否则应使用fdOutRange检查范围
        Task* newTask();
        Task* newTask(const Task &other,addr_t ustack=0u);
        Task* newKTask(prior_t prior=0);
        void print();
        int fdAlloc(shared_ptr<File> a_file, int a_fd = 0, bool a_appoint = false);
        /// @todo rtval
        xlen_t brk(xlen_t addr);
        void exit(int status);
        void zombieExit();
    private:
        xlen_t newUstack();
        auto newTrapframe();
        inline void addTask(Task* task){ tasks.insert(task); }
    };
    struct Task:public IdManagable,public Scheduable{ // a.k.a. kthread
        enum class Priv:uint8_t{
            User,Kernel,AlwaysKernel
        };
        Context ctx;
        KContext kctx;
        stack<KContext> kctxs;
        const pid_t proc;
        Priv lastpriv;
        SigMask sigmask = 0, sigpending = 0;
        shared_ptr<SigInfo> siginfos[numSigs] = { nullptr };
        SigStack sigstack = {};
        inline bool onSigStack() { return ctx.sp()-(xlen_t)sigstack.ss_sp < sigstack.ss_size; }
        // void accept();
        Process *getProcess();
        inline tid_t tid() { return id; }
        inline Task(tid_t tid,prior_t pri,tid_t proc):IdManagable(tid),Scheduable(pri),proc(proc),lastpriv(Priv::User){
            ctx.x(2)=UserStackDefault; //x2, sp
            kctx.satp=getProcess()->satp();
            kctx.sp()=trapstack();
        }
        inline Task(prior_t pri,tid_t proc):Task(id,pri,proc){}
        inline Task(tid_t tid,const Task &other,pid_t proc):IdManagable(tid),Scheduable(other.prior),proc(proc),lastpriv(Priv::User){
            ctx=other.ctx;
            kctx=other.kctx;
            kctx.satp=getProcess()->satp();
            kctx.sp()=trapstack();
        }
        inline Task(const Task &other,pid_t proc):Task(id,other,proc){}
        FORCEDINLINE inline xlen_t trapstack(){return reinterpret_cast<xlen_t>(this)+TrapframePages*vm::pageSize;}

        void switchTo();
        void sleep();

        inline string toString(bool detail=false) const{
            if(detail)return klib::format("Task<%d> priv=%d kctx=%s",id,lastpriv,kctx.toString());
            else return klib::format("Task<%d> priv=%d sp=%x ksp=%x pc=%x kra=%x",id,lastpriv,ctx.gpr[1],kctx.gpr[1],ctx.pc,kctx.gpr[0]);
        }

        template<typename ...Ts>
        static Task* createTask(ObjManager<Task> &mgr,xlen_t buff,Ts&& ...args);
        void operator delete(ptr_t task){}
        FORCEDINLINE inline static Task* gprToTask(ptr_t gpr){return reinterpret_cast<Task*>(gpr-offsetof(Task,ctx.gpr));}
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
        vector<Process*> getChilds(pid_t pid){
            vector<Process*> rt;
            for(int i=0;i<512;i++){
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
    inline bool fdOutRange(int a_fd) { return (a_fd<0) || (a_fd>=proc::mOFiles); }
} // namespace proc


#endif