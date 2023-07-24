#ifndef __SYS_RESOURCE_H__
#define __SYS_RESOURCE_H__

#include <sys/time.h>
#include "common.h"

#define RLIM_INFINITY 0xffffffffffffffffUL

namespace resource {
    typedef struct Rusage {
        timeval ru_utime;        /* user CPU time used */
        timeval ru_stime;        /* system CPU time used */
        long   ru_maxrss;        /* maximum resident set size */
        long   ru_ixrss;         /* integral shared memory size */
        long   ru_idrss;         /* integral unshared data size */
        long   ru_isrss;         /* integral unshared stack size */
        long   ru_minflt;        /* page reclaims (soft page faults) */
        long   ru_majflt;        /* page faults (hard page faults) */
        long   ru_nswap;         /* swaps */
        long   ru_inblock;       /* block input operations */
        long   ru_oublock;       /* block output operations */
        long   ru_msgsnd;        /* IPC messages sent */
        long   ru_msgrcv;        /* IPC messages received */
        long   ru_nsignals;      /* signals received */
        long   ru_nvcsw;         /* voluntary context switches */
        long   ru_nivcsw;        /* involuntary context switches */
    } RUsg;
    typedef enum resource_t {
        RLIMIT_AS,
        RLIMIT_CORE,
        RLIMIT_CPU,
        RLIMIT_DATA,
        RLIMIT_FSIZE,
        RLIMIT_LOCKS,
        RLIMIT_MEMLOCK,
        RLIMIT_MSGQUEUE,
        RLIMIT_NICE,
        RLIMIT_NOFILE,
        RLIMIT_NPROC,
        RLIMIT_RSS,
        RLIMIT_RTPRIO,
        RLIMIT_RTTIME,
        RLIMIT_SIGPENDING,
        RLIMIT_STACK,
        RLIMIT_NLIMITS,
    } RSrc;
    typedef struct rlimit {
        rlim_t rlim_cur;
        rlim_t rlim_max;
    } RLim;
}  // namespace resource

#endif