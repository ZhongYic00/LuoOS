#include "ipc.hh"
#include "kernel.hh"
// #define moduleLevel LogLevel::debug

namespace pipe
{

    void Pipe::sleep(){
        waiting.push_back(kHartObj().curtask);
        syscall::sleep();
    }
    void Pipe::wakeup(){
        for(auto task:waiting){
            Log(debug,"Pipe::wakeup Task<%d>\n",task->id);
            kGlobObjs->scheduler->wakeup(task);
        }
        while(!waiting.empty())waiting.pop_front();
    }
} // namespace ipc

namespace signal
{
    void send(Process &proc,int num,unique_ptr<SignalInfo>& info){
        for(auto tsk:proc.tasks){
            if(!tsk->block[num])
                return send(*tsk,num,info); 
        }
    }
    void send(Task &task,int num,unique_ptr<SignalInfo>& info){
        if(!task.pending[num]){
            task.pendingmask[num]=1;
            task.pending[num]=std::move(info);
        }
        return ;
    }
} // namespace signal
