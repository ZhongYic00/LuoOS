#ifndef SCHED_HH__
#define SCHED_HH__
#include "klib.hh"

namespace sched
{
    enum State:short{
        Init,Ready,Running,Pending,Exit,
    };
    typedef short prior_t;
    struct Scheduable{
        prior_t prior;
        State state;
        Scheduable(prior_t prior):prior(prior),state(Init){}
    };
    using klib::list;
    class Scheduler{
        list<Scheduable*,true> ready;
        list<Scheduable*> pending;
        klib::list<Scheduable*,true>::iterator cur;
    public:
        Scheduable *next();
        Scheduler();
        void add(Scheduable *elem);
        void wakeup(Scheduable *elem);
        void sleep(Scheduable *elem);
    };
} // namespace sched

extern void schedule();

#endif