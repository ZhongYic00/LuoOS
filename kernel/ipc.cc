#include "ipc.hh"
#include "kernel.hh"
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

        }
    }
} // namespace ipc
