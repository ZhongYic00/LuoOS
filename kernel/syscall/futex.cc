#include "common.h"
#include "kernel.hh"
#include "lock.hh"

#include <linux/futex.h>
namespace syscall
{
int futex(int *uaddr, int futex_op, int val,
                 const struct timespec *timeout,   /* or: uint32_t val2 */
                 int *uaddr2, int val3){
    auto curproc=kHartObj().curtask->getProcess();
    /// @todo not good practice, may use (vmo,offset) instead
    auto paddr=curproc->vmar.transaddr((addr_t)uaddr);
    switch(futex_op&FUTEX_CMD_MASK){
        case FUTEX_WAIT:{
            // test value atomically
            auto &cv=kGlobObjs->futexes[paddr];
            {
                /// @bug @note cannot cast to atomic
                auto &curval=*reinterpret_cast<std::atomic_int32_t*>((ptr_t)paddr);
                if(curval.load()!=val) return -EAGAIN;
            }
            if(timeout){
                timespec ts;
                curproc->vmar[(addr_t)timeout]>>ts;
            } else cv.wait();
            return 0;
        }
        case FUTEX_WAKE:{
            /// @bug what's the right rtval?
            if(!kGlobObjs->futexes.count(paddr))
                return 0;
            auto &cv=kGlobObjs->futexes[paddr];
            if(val==std::numeric_limits<int>::max()){
                val=cv.waiters();
                cv.notify_all();
            } else for(auto i=0;i<val;i++)
                cv.notify_one();
            return val;
        }
        case FUTEX_CMP_REQUEUE:{
            auto &curval=*reinterpret_cast<std::atomic_int32_t*>((ptr_t)paddr);
            if(curval.load()!=val3) return -EAGAIN;
        }
        case FUTEX_REQUEUE:{
            auto &cv=kGlobObjs->futexes[paddr];
            if(val==std::numeric_limits<int>::max()){
                Log(error,"unexpected combination of FUTEX_REQUEUE and INT_MAX");
                return -EINVAL;
            } else for(auto i=0;i<val;i++)
                cv.notify_one();
            auto paddr2=curproc->vmar.transaddr((addr_t)uaddr2);
            kGlobObjs->futexes[paddr2]=cv;
            kGlobObjs->futexes.erase(paddr);
            return val;
        }
        default:
            Log(error,"unimplemented! futex op=%d",futex_op);
            return -EINVAL;
    }
}
    
} // namespace syscall
