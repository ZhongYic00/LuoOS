#ifndef __TYPES_H
#define __TYPES_H

typedef void* ptr_t;
typedef __UINT64_TYPE__ xlen_t;
typedef __UINT32_TYPE__ word_t;
typedef __UINT8_TYPE__ uint8_t;
typedef void (*hook_t)(void);
typedef xlen_t (*syscall_t)(void);
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
#define NULL 0

#include <stdarg.h>
#include <limits.h>

#endif