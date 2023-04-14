
#define BIT(x) (1ll<<x)

#if defined(__GNUC__)
#define FORCEDINLINE  __attribute__((always_inline))
#else 
#define FORCEDINLINE
#endif

#define IFDEF(cond,x) if(cond){x}
#define DBG(x) IFDEF(DEBUG,x)
#define IFTEST(x) IFDEF(GUEST,x)

#define DEBUG 0