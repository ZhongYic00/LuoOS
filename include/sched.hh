#ifndef SCHED_HH__
#define SCHED_HH__
#include "klib.hh"

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
    using klib::list;
    class Scheduler{
        list<Scheduable*,true> ready[maxPrior+1];
        list<Scheduable*> pending;
        klib::list<Scheduable*,true>::iterator cur[maxPrior+1];
    public:
        Scheduable *next();
        Scheduler();
        void add(Scheduable *elem);
        void remove(Scheduable *elem);
        void wakeup(Scheduable *elem);
        void sleep(Scheduable *elem);
    };
} // namespace sched

extern void schedule();

#endif