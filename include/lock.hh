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


#endif