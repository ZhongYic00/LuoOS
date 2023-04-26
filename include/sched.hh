#ifndef SCHED_HH__
#define SCHED_HH__
#include "klib.hh"

namespace sched
{
    enum State:short{
        Init,Ready,Running,Pending,Exit,
    };
    typedef int tid_t;
    typedef short prior_t;
    struct Scheduable{
        tid_t id;
        prior_t prior;
        State state;
        Scheduable(tid_t id,prior_t prior):id(id),prior(prior),state(Init){}
    };
    using klib::list;
    class Scheduler{
        list<Scheduable*> ready;
        list<Scheduable*> pending;
        klib::ListNode<Scheduable*> *cur;
    public:
        Scheduable *next();
        // Scheduler();
        void add(Scheduable *elem);
    };
} // namespace sched


#endif