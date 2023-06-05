#ifndef __TYPES_H
#define __TYPES_H

typedef void* ptr_t;
typedef __UINT64_TYPE__ xlen_t;
typedef __UINT32_TYPE__ word_t;
typedef __UINT8_TYPE__ uint8_t;
typedef void (*hook_t)(void);
typedef xlen_t (*syscall_t)(void);
// 搬FAT时顺便搬过来的
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef unsigned short wchar;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;
typedef long int64;
typedef unsigned long uintptr_t;
typedef uint64 pde_t;
typedef uint64 pid_t;
typedef unsigned int mode_t;
typedef uint32 uid_t;
typedef uint32 gid_t;
typedef uint32 dev_t;
typedef uint64 ino_t;
typedef uint32 nlink_t;
typedef uint32 off_t;
typedef uint32 blkcnt_t;
typedef uint32 blksize_t;
typedef long time_t;
struct UtSName {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};
class TimeSpec {
    private:
        time_t m_tv_sec;  /* 秒 */
        long m_tv_nsec; /* 纳秒, 范围在0~999999999 */
    public:
        TimeSpec():m_tv_sec(1), m_tv_nsec(1) {}
        TimeSpec(time_t a_tv_sec, long a_tv_nsec):m_tv_sec(a_tv_sec), m_tv_nsec(a_tv_nsec) {}
};
#define NULL 0
#define readb(addr) (*(volatile uint8 *)(addr))     
#define readw(addr) (*(volatile uint16 *)(addr))    
#define readd(addr) (*(volatile uint32 *)(addr))    
#define readq(addr) (*(volatile uint64 *)(addr))    
#define writeb(v, addr)                      \
    {                                        \
        (*(volatile uint8 *)(addr)) = (v); \
    }
#define writew(v, addr)                       \
    {                                         \
        (*(volatile uint16 *)(addr)) = (v); \
    }
#define writed(v, addr)                       \
    {                                         \
        (*(volatile uint32 *)(addr)) = (v); \
    }
#define writeq(v, addr)                       \
    {                                         \
        (*(volatile uint64 *)(addr)) = (v); \
    }
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

#include <stddef.h>
#include <stdarg.h>
#include <limits.h>

#define INTERVAL     (390000000 / 100) // timer interrupt interval
#define CLK_FREQ 		8900000

#endif