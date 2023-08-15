#ifndef LOCK_HH__
#define LOCK_HH__

#include "klib.hh"
#include <EASTL/chrono.h>
// #define moduleLevel debug

namespace proc{struct Task;}
namespace kernel{int threadId();}

namespace semaphore
{
    class Semaphore{
        /// @todo use atomic
        int count;
        list<proc::Task*> waiting;
    public:
        Semaphore(int init=1):count(init){}
        void req();
        void rel();
    };
} // namespace semaphore

namespace mutex
{
    template<bool reentrantalbe=true,typename count_t=xlen_t,xlen_t maxloops=std::numeric_limits<count_t>::max(),typename atomic_base_t=uint32_t>
    class spinlock{
        std::atomic<atomic_base_t> spin;
        int count;
    public:
        spinlock():spin(0),count(0){}
        inline bool lock(){
            Log(debug,"lock(%p,%d,%d)",this,spin.load());
            /// @todo optimize, use normal read to reduce buslock overhead
            /// @todo use thread class here
            atomic_base_t lockedby;
            if constexpr(reentrantalbe)lockedby=kernel::threadId();
            else lockedby=1;
again:      atomic_base_t expected=0;
            bool b=true;
            for(xlen_t n=maxloops;
                b && n>0;
                    n--){
                        expected=0;
                        b=!spin.compare_exchange_strong(expected,lockedby);
                        if constexpr(reentrantalbe){
                            b=b&&!spin.compare_exchange_strong(lockedby,lockedby);
                        }
                    }
            if(reentrantalbe && !b)count++;
            if(!b)return true;
            if (maxloops==std::numeric_limits<count_t>::max())
                goto again;
            else return false;
            // giveup
        }
        inline void unlock(){
            if constexpr(!reentrantalbe)spin.store(0);
            else {
                count--;
                if(count==0)spin.store(0);
            }
            Log(debug,"unlock(%p,%d)",this,spin.load());
        }
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
    template <typename T,typename L>
    class LockedPtr: lock_guard<L>
    {
    public:
        LockedPtr(T& initRef,L& lock)
            : ref(initRef), lock_guard<L>(lock){
                Log(debug,"lockedptr(ref=%p)",&ref);
            }
        ~LockedPtr(){
            Log(debug,"~lockedptr(ref=%p)",&ref);
        }
        T* operator->(){return &ref;}
        T& operator*() {return ref;}

    // .. operator overloads for -> and *
    private:
        T& ref;
    };

    template<typename T,typename L=spinlock<>>
    class ObjectGuard{
        T& obj;
        L lock;
    public:
        ObjectGuard(T *obj):obj(*obj){}
        ObjectGuard(T& obj):obj(obj){}
        LockedPtr<T,L> operator->(){return LockedPtr(obj,lock);}
        LockedPtr<T,L> get(){return LockedPtr(obj,lock);}
        LockedPtr<T,L> operator*() {return LockedPtr(obj,lock);}
        T* const ro() const{return &obj;}
    };
    template<typename T,typename L=spinlock<>>
    class LockedObject:public ObjectGuard<T,L>{
        T object;
    public:
        template<typename ...Ts>
        explicit LockedObject(Ts&& ...args):object(args...),ObjectGuard<T,L>(object){}
    };
} // namespace mutex
namespace condition_variable
{
    class condition_variable{
        list<proc::Task*> waiting;
        void notify_specific(proc::Task* task);
    public:
        condition_variable(){}
        void notify_one();
        void notify_all();
        void wait();
        inline int waiters() const {return waiting.size();}
        bool wait_for(const eastl::chrono::nanoseconds& dura);
    };
} // namespace condition_variable


using namespace mutex;


#endif