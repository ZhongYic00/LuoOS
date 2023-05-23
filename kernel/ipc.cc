#include "ipc.hh"
#include "kernel.hh"
// #define moduleLevel LogLevel::debug
namespace syscall{
extern int sleep();
}
namespace pipe
{

    void Pipe::sleep(){
        waiting.push_back(kHartObjs.curtask);
        syscall::sleep();
    }
    void Pipe::wakeup(){
        for(auto task:waiting){
            Log(debug,"Pipe::wakeup Task<%d>\n",task->id);
            kGlobObjs.scheduler.wakeup(task);
        }
        while(!waiting.empty())waiting.pop_front();
    }
} // namespace ipc
