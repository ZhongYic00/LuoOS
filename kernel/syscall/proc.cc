#include "syscall.hh"
#include "common.h"
#include "kernel.hh"
#include "time.hh"
#include "ld.hh"
#include <linux/sched.h>
#include <sys/resource.h>

namespace syscall
{
    using namespace sys;
    sysrt_t clone(unsigned long flags, void *stack,
                      int *parent_tid, unsigned long tls,
                      int *child_tid){
        auto &cur=kHartObj().curtask;
        auto &ctx=kHartObj().curtask->ctx;

        proc::Task* thrd;
        if(flags&(CLONE_VM|CLONE_THREAD|CLONE_SIGHAND)){
            auto curproc=kHartObj().curtask->getProcess();
            thrd=curproc->newTask(*kHartObj().curtask,(addr_t)stack);
            thrd->ctx.a0()=0;
        } else {
            auto pid=proc::clone(kHartObj().curtask);
            thrd=(**kGlobObjs->procMgr)[pid]->defaultTask();
            if(stack)thrd->ctx.sp()=(addr_t)stack;
            Log(debug,"clone curproc=%d, new proc=%d",kHartObj().curtask->getProcess()->pid(),pid);
        }
        
        // set/clear child tid
        if(flags&CLONE_CHILD_SETTID && child_tid)
            thrd->getProcess()->vmar[(addr_t)child_tid]<<thrd->tid();
        if(flags&CLONE_PARENT_SETTID && parent_tid)
            cur->getProcess()->vmar[(addr_t)parent_tid]<<thrd->tid();
        if(flags&CLONE_SETTLS)
            thrd->ctx.tp()=tls;
        if(flags&CLONE_CHILD_CLEARTID)
            thrd->attrs.clearChildTid=child_tid;
        return flags&CLONE_THREAD?thrd->tid():thrd->getProcess()->pid();
    }
    xlen_t execve_(shared_ptr<fs::File> file,vector<klib::ByteArray> &args,vector<klib::ByteArray> &envs){
        auto &ctx=kHartObj().curtask->ctx;
        /// @todo reset cur proc vmar, refer to man 2 execve for details
        auto curproc=kHartObj().curtask->getProcess();
        curproc->vmar.reset();
        /// @todo destroy other threads
        /// @todo reset curtask cpu context
        ctx=proc::Context();
        // static_cast<proc::Context>(kHartObj().curtask->kctx)=proc::Context();
        ctx.sp()=proc::UserStackDefault;
        /// load elf
        auto [entry,brk,info]=ld::loadElf(file,curproc->vmar);
        ctx.pc=entry;
        curproc->heapTop=curproc->heapBottom=brk;
        /// setup stack
        ArrayBuff<xlen_t> argv(args.size()+1);
        vector<addr_t> envps;
        int argc=0;
        auto ustream=curproc->vmar[ctx.sp()];
        ustream.reverse=true;
        xlen_t endMarker=0x114514;
        ustream<<endMarker;
        ustream<<0ul;
        auto dlRandomAddr=ustream.addr();
        for(auto arg:args){
            ustream<<arg;
            argv.buff[argc++]=ustream.addr();
        }
        for(auto env:envs){
            ustream<<env;
            envps.push_back(ustream.addr());
        }
        // ctx.sp()=ustream.addr();
        argv.buff[argc]=0;
        // ctx.sp()-=argv.len*sizeof(xlen_t);
        // ctx.sp()-=ctx.sp()%16;
        vector<Elf64_auxv_t> auxv;
        /* If the main program was already loaded by the kernel,
        * AT_PHDR will point to some location other than the dynamic
        * linker's program headers. */
        if(info.phdr){
            auxv.push_back({AT_PHDR,info.phdr});
            auxv.push_back({AT_PHENT,info.e_phentsize});
            auxv.push_back({AT_PHNUM,info.e_phnum});
        }
        auxv.push_back({AT_PAGESZ,vm::pageSize});
        auxv.push_back({AT_BASE,proc::interpreterBase});
        auxv.push_back({AT_ENTRY,info.e_entry});
        auxv.push_back({AT_RANDOM,dlRandomAddr});
        auxv.push_back({AT_NULL,0});
        ustream<<ArrayBuff(auxv.data(),auxv.size());
        ustream<<nullptr;
        ustream<<envps;
        ustream<<argv;
        auto argvsp=ustream.addr();
        // assert(argvsp==ctx.sp());
        /// @bug alignment?
        // ctx.sp()-=8;
        ustream<<(xlen_t)argc;
        auto argcsp=ustream.addr();
        ctx.sp()=ustream.addr();
        // assert(argcsp==ctx.sp());
        Log(debug,"$sp=%x, argc@%x, argv@%x",ustream.addr(),argcsp,argvsp);
        /// setup argc, argv
        return ctx.sp();
    }
    sysrt_t execve(addr_t pathuva,addr_t argv,addr_t envp){
        auto &cur=kHartObj().curtask;
        auto &ctx=cur->ctx;
        auto curproc=cur->getProcess();
        using namespace fs;

        /// @brief get executable from path
        ByteArray pathbuf = curproc->vmar.copyinstr(pathuva, FAT32_MAX_PATH);
        // string path((char*)pathbuf.buff,pathbuf.len);
        char *path=(char*)pathbuf.buff;
        curproc->exe=Path(path).pathAbsolute();

        Log(debug,"execve(path=%s,)",path);
        shared_ptr<DEntry> Ent=Path(path).pathSearch();
        auto file=make_shared<File>(Ent,fs::FileOp::read);
        
        vector<ByteArray> args,envs;

        // check whether elf or script
        auto interprtArg="sh\0";
        if(!ld::isElf(file)){
            string interpreter="/busybox";
            file=make_shared<File>(Path(interpreter).pathSearch(),fs::FileOp::read);
            args.push_back(ByteArray((uint8_t*)interprtArg,strlen(interprtArg)+1));
        }

        /// @brief get args
        xlen_t str;
        do{
            curproc->vmar[argv]>>str;
            if(!str)break;
            auto buff=curproc->vmar.copyinstr(str,200);
            args.push_back(buff);
            argv+=sizeof(char*);
        }while(str!=0);
        /// @brief get envs
        auto ustream=curproc->vmar[envp];
        if(envp) do{
            ustream>>str;
            if(!str)break;
            auto buff=curproc->vmar.copyinstr(str,100);
            envs.push_back(buff);
        }while(str!=0);
        return execve_(file,args,envs);
    }
    sysrt_t setTidAddress(int *tidptr){
        auto &cur=kHartObj().curtask;
        /// @bug how to use this attr? one-off?
        cur->attrs.clearChildTid=tidptr;
        return cur->id;
    }

    sysrt_t getrusage(int who, struct rusage *usage){
        auto &cur=kHartObj().curtask;
        switch(who){
            case RUSAGE_SELF:{
                auto tms=cur->getProcess()->stats.ti;
                struct rusage usg={
                    .ru_utime=timeservice::duration2timeval(timeservice::ticks2chrono(tms.tms_utime)),
                    .ru_stime={0,0},
                };
                cur->getProcess()->vmar[(addr_t)usage]<<usg;
                return statcode::ok;
            }
            case RUSAGE_CHILDREN:{

                return statcode::ok;
            }
            default:
                Log(error,"who is invalid");
                return Err(EINVAL);
        }
    }
    sysrt_t exitGroup(int status){
        auto cur=kHartObj().curtask;
        auto curproc=cur->getProcess();
        for(auto task:curproc->tasks){
            task->state=sched::Zombie;
            kGlobObjs->scheduler->remove(task);
        }
        curproc->exit(status);
        return 0;
    }
    sysrt_t exit(int status){
        auto cur=kHartObj().curtask;
        cur->exit(status);
        return 0;
    }
    sysrt_t waitpid(pid_t pid,xlen_t wstatus,int options){
        Log(debug,"waitpid(pid=%d,options=%d)",pid,options);
        auto curproc=kHartObj().curtask->getProcess();
        proc::Process* target=nullptr;
        if(pid==-1){
            // get child procs
            // every child wakes up parent at exit?
            auto childs=kGlobObjs->procMgr->getChilds(curproc->pid());
            while(!childs.empty()){
                for(auto child:childs){
                    if(child->state==sched::Zombie){
                        target=child;
                        break;
                    }
                }
                if(target)break;
                kernel::sleep();;
                childs=kGlobObjs->procMgr->getChilds(curproc->pid());
            }
        }
        else if(pid>0){
            auto proc=(**kGlobObjs->procMgr)[pid];
            if(!proc)panic("should not happen");
            while(proc->state!=sched::Zombie){
                // proc add hook
                kernel::sleep();;
            }
            target=proc;
        }
        else if(pid==0)panic("waitpid: unimplemented!"); // @todo 每个回收的子进程都更新父进程的ti
        else if(pid<0)panic("waitpid: unimplemented!");
        if(target==nullptr)return statcode::err;
        {   // update curproc stats
            /// @note should be here?
            auto &stats=curproc->stats,&cstats=target->stats;
            stats.ti.tms_cstime+=cstats.ti.tms_stime+cstats.ti.tms_cstime;
            stats.ti.tms_cutime+=cstats.ti.tms_utime+cstats.ti.tms_cutime;
        }
        auto rt=target->pid();
        if(wstatus)curproc->vmar[wstatus]<<(int)(target->exitstatus<<8);
        target->zombieExit();
        return rt;
    }
} // namespace syscall
