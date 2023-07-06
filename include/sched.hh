#ifndef SCHED_HH__
#define SCHED_HH__
#include "klib.hh"
#include "EASTL/list.h"

namespace sched
{
    enum State:short{
        Init,Ready,Running,Pending,Zombie,
    };
    typedef short prior_t;
    constexpr int maxPrior=1;
    struct Scheduable{
        prior_t prior;
        State state;
        Scheduable(prior_t prior):prior(prior),state(Init){}
    };
    using eastl::list;
    class Scheduler{
        list<Scheduable*> ready[maxPrior+1];
        list<Scheduable*> pending;
    public:
        Scheduable *next(Scheduable *prev);
        Scheduler();
        void add(Scheduable *elem);
        void remove(Scheduable *elem);
        void wakeup(Scheduable *elem);
        void sleep(Scheduable *elem);
    };
} // namespace sched

extern void schedule();

#endif