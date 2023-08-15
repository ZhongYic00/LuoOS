#ifndef TIME_HH__
#define TIME_HH__

#include <EASTL/chrono.h>
#include <EASTL/tuple.h>
#include <EASTL/priority_queue.h>
#include <EASTL/set.h>
#include <sys/time.h>

namespace timeservice
{
    using namespace eastl::chrono_literals;
    using namespace eastl::chrono;
    constexpr auto mtickDuration=100ns;
    constexpr auto tickHz=50;
    constexpr auto tickDuration=1000ms/tickHz;
    constexpr auto mticksPerSec=1000000000ns/mtickDuration;
    typedef duration<long long,eastl::ratio<1,mticksPerSec>> mtimesec;

    constexpr inline auto ticks2chrono(clock_t ticks){
        return ticks*tickDuration;
    }
    inline auto duration2timeval(nanoseconds dura){
        auto s=duration_cast<seconds>(dura);
        if(s>dura)s-=1s;
        return timeval{s.count(),duration_cast<microseconds>(dura-s).count()};
    }
    inline auto duration2timespec(nanoseconds dura){
        auto s=duration_cast<seconds>(dura);
        if(s>dura)s-=1s;
        return timespec{s.count(),duration_cast<nanoseconds>(dura-s).count()};
    }

    class Timer{
        using timepoint_t=time_point<system_clock,nanoseconds>;
        using internal_tp=time_point<system_clock,mtimesec>;
        using callback_t=eastl::function<void()>;
        typedef eastl::tuple<internal_tp,int,int> Key;
        typedef eastl::pair<Key,callback_t> value_t;
        struct first_less{
            template<typename T>
            bool operator()(T const& lhs,T const& rhs) const {
                return lhs.first>rhs.first;
            }
        };
        eastl::priority_queue<value_t,eastl::vector<value_t>,first_less> events;
        // eastl::set<value_t,first_less> events;
    public:
        void add(timepoint_t tp,callback_t func,int prior);
        inline void setTimeout(nanoseconds dura,callback_t func,int prior=1){
            add(system_clock::now()+dura,func,prior);
        }
        inline void setInterval(nanoseconds dura,callback_t func,int prior=1){
            callback_t itvfunc=[func,dura,prior,this]()->void{
                this->setInterval(dura,func,prior);
                func();
            };
            setTimeout(dura,itvfunc,prior);
        }
        void next();
    };
} // namespace timeservice

#endif