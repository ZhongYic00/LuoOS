#pragma once
#define BIT(x) (1ll<<x)

#if defined(__GNUC__)
#define FORCEDINLINE  __attribute__((always_inline))
#else 
#define FORCEDINLINE
#endif

enum LogLevel{
    trace,debug,info,warning,error,
    nlevels
};

#define IFDEF(cond,x) if(cond){x}
#define DBG(x) IFDEF(moduleLevel<=LogLevel::debug,x)
#define TRACE(x) IFDEF(moduleLevel<=LogLevel::trace,x)
#define IFTEST(x) IFDEF(GUEST,x)

extern int enableLevel;
inline int readHartId(){register int rt asm("tp");return rt;}
#ifndef moduleLevel
    #define moduleLevel LogLevel::error
#endif
#define Log(level,fmt,...) \
    if(level>=enableLevel && level>=moduleLevel){kLogger.log(level,("[%d]"__FILE__":%d:%s::\t" fmt "\n"),readHartId(),__LINE__,__FUNCTION__,##__VA_ARGS__);}

#define DEBUG 0