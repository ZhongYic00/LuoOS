#include "ipc.hh"
#include "kernel.hh"

extern int syssleep();
namespace pipe
{

    void Pipe::sleep(){
        waiting.push_back(kHartObjs.curtask);
        // kHartObjs.curtask.sleep();
        syssleep();
    }
    void Pipe::wakeup(){
        for(auto task:waiting){

        }
    }
} // namespace ipc
