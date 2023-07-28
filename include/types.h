#ifndef __TYPES_H
#define __TYPES_H

#include <sys/types.h>

typedef void* ptr_t;
typedef __UINT64_TYPE__ xlen_t;
typedef __UINT32_TYPE__ word_t;
typedef __UINT8_TYPE__ uint8_t;
typedef void (*hook_t)(void);
typedef xlen_t sysrt_t;
typedef sysrt_t (*syscall_t)(xlen_t,xlen_t,xlen_t,xlen_t,xlen_t,xlen_t);
typedef xlen_t addr_t;
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
typedef long long loff_t;
typedef int tid_t;
typedef tid_t pid_t;
typedef uint64 rlim_t;
// 已存在
// typedef tid_t uid_t;
// typedef tid_t gid_t;
#define NULL 0
#define typeof __typeof__

#include <stdarg.h>
#include <limits.h>

#endif