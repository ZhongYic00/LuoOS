#ifndef LOCK_HH__
#define LOCK_HH__

#include "klib.hh"

namespace proc{struct Task;}

namespace semaphore
{
    class Semaphore{
        /// @todo use atomic
        int count;
        klib::list<proc::Task*> waiting;
    public:
        Semaphore(int init=1):count(init){}
        void req();
        void rel();
    };
} // namespace semaphore

namespace mutex
{
    template<typename count_t=xlen_t,xlen_t maxloops=std::numeric_limits<count_t>::max(),typename atomic_base_t=uint32_t>
    class spinlock{
        std::atomic<atomic_base_t> spin;
    public:
        bool lock(){
            /// @todo optimize, use normal read to reduce buslock overhead
again:      atomic_base_t expected=0;
            bool b=true;
            for(xlen_t n=maxloops;
                b && n>0;
                    n--){
                        expected=0;
                        b=!spin.compare_exchange_strong(expected,1);
                    }
            if(!b)return true;
            if (maxloops==std::numeric_limits<count_t>::max())
                goto again;
            else return false;
            // giveup
        }
        void unlock(){spin.store(0);}
    };

    class mutex{
    };

    template<typename lock_t>
    class lock_guard{
        lock_t& lock;
    public:
        explicit lock_guard(lock_t& lock_):lock(lock_){lock.lock();}
        ~lock_guard(){lock.unlock();}

        lock_guard(const lock_guard&) = delete;
        lock_guard& operator=(const lock_guard&) = delete;
    };
} // namespace mutex


#endif