#ifndef TIME_HH__
#define TIME_HH__

#include <EASTL/chrono.h>

namespace timeservice
{
    using namespace eastl::chrono_literals;
    using namespace eastl::chrono;
    constexpr auto tickHz=50;
    constexpr auto tickDuration=1000ms/tickHz;

    constexpr inline auto ticks2chrono(clock_t ticks){
        return ticks*tickDuration;
    }
    inline auto duration2timeval(nanoseconds dura){
        auto s=duration_cast<seconds>(dura);
        if(s>dura)s-=1s;
        return timeval{s.count(),duration_cast<microseconds>(dura-s).count()};
    }
} // namespace timeservice

#endif