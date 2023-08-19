#include "time.hh"
#include "kernel.hh"
#include "sbi.hh"
#include "rvcsr.hh"

#define moduleLevel debug

namespace timeservice
{
    void Timer::add(timepoint_t tp,callback_t func,int prior){
        auto mtp=time_point_cast<mtimesec>(tp);
        auto key=eastl::make_tuple(mtp,prior,(tp-mtp).count());
        events.push(eastl::pair{key,func});
    }
    constexpr auto tole=100000000ns;
    void Timer::next(){
        while(!events.empty()){
            auto [mtp,prior,tp]=events.top().first;
            auto now=time_point_cast<mtimesec>(system_clock::now());
            if(mtp>now){
                sbi_set_timer(mtp.time_since_epoch().count());
                return ;
            } else {
                if(mtp<now-tole)
                    Log(error,"can't catch up");
                auto func=events.top().second;
                events.pop();
                func();
            }
        }
        assert(!events.empty());
    }

    uint64_t getTicks(){
        uint64_t timerTicks;
        csrRead(time,timerTicks);
        return timerTicks;
    }
} // namespace timeservice