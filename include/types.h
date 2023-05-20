#ifndef __TYPES_H
#define __TYPES_H

typedef void* ptr_t;
typedef __UINT64_TYPE__ xlen_t;
typedef __UINT32_TYPE__ word_t;
typedef __UINT8_TYPE__ uint8_t;
typedef void (*hook_t)(void);
typedef int (*syscall_t)(void);

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
// typedef unsigned int size_t;
typedef uint32 dev_t;
typedef uint64 ino_t;
typedef uint32 nlink_t;
typedef uint32 off_t;
typedef uint32 blkcnt_t;
typedef uint32 blksize_t;
typedef struct
{
    uint64 sec;  // 自 Unix 纪元起的秒数
    uint64 usec; // 微秒数
} TimeVal;
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

#endif