#ifndef KLIB_H__
#define KLIB_H__


#include "common.h"


#define saveContext() __asm__("sd x1,0(t6)\n sd x2,8(t6)\n sd x3,16(t6)\n sd x4,24(t6)\n sd x5,32(t6)\n sd x6,40(t6)\n sd x7,48(t6)\n sd x8,56(t6)\n sd x9,64(t6)\n sd x10,72(t6)\n sd x11,80(t6)\n sd x12,88(t6)\n sd x13,96(t6)\n sd x14,104(t6)\n sd x15,112(t6)\n sd x16,120(t6)\n sd x17,128(t6)\n sd x18,136(t6)\n sd x19,144(t6)\n sd x20,152(t6)\n sd x21,160(t6)\n sd x22,168(t6)\n sd x23,176(t6)\n sd x24,184(t6)\n sd x25,192(t6)\n sd x26,200(t6)\n sd x27,208(t6)\n sd x28,216(t6)\n sd x29,224(t6)\n sd x30,232(t6)\n")
#define restoreContext() __asm__("ld x1,0(t6)\n ld x2,8(t6)\n ld x3,16(t6)\n ld x4,24(t6)\n ld x5,32(t6)\n ld x6,40(t6)\n ld x7,48(t6)\n ld x8,56(t6)\n ld x9,64(t6)\n ld x10,72(t6)\n ld x11,80(t6)\n ld x12,88(t6)\n ld x13,96(t6)\n ld x14,104(t6)\n ld x15,112(t6)\n ld x16,120(t6)\n ld x17,128(t6)\n ld x18,136(t6)\n ld x19,144(t6)\n ld x20,152(t6)\n ld x21,160(t6)\n ld x22,168(t6)\n ld x23,176(t6)\n ld x24,184(t6)\n ld x25,192(t6)\n ld x26,200(t6)\n ld x27,208(t6)\n ld x28,216(t6)\n ld x29,224(t6)\n ld x30,232(t6)\n")
#define saveReg(reg,dst,off) asm("sd "#reg","#off"(%0)"::"r"(dst))
// #define saveContextTo(gpr) {saveReg(x1,gpr,0);saveReg(x2,gpr,8);saveReg(x3,gpr,16);saveReg(x4,gpr,24);saveReg(x5,gpr,32);saveReg(x6,gpr,40);saveReg(x7,gpr,48);saveReg(x8,gpr,56);saveReg(x9,gpr,64);saveReg(x10,gpr,72);saveReg(x11,gpr,80);saveReg(x12,gpr,88);saveReg(x13,gpr,96);saveReg(x14,gpr,104);saveReg(x15,gpr,112);saveReg(x16,gpr,120);saveReg(x17,gpr,128);saveReg(x18,gpr,136);saveReg(x19,gpr,144);saveReg(x20,gpr,152);saveReg(x21,gpr,160);saveReg(x22,gpr,168);saveReg(x23,gpr,176);saveReg(x24,gpr,184);saveReg(x25,gpr,192);saveReg(x26,gpr,200);saveReg(x27,gpr,208);saveReg(x28,gpr,216);saveReg(x29,gpr,224);saveReg(x30,gpr,232);}
#define saveContextTo(gpr) {register ptr_t t6 asm("t6")=gpr; saveContext();}

#ifdef GUEST

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#else

#if defined(__cplusplus)
// #include "klib.hh"
#endif

extern "C" {
#define STDIO 1
#define STRING 0

extern void (*puts)(const char *s);
void halt(int errno=0);
void panic(char *s);
// string.h
#ifdef STRING
void  *memset    (void *s, int c, size_t n);
void  *memcpy    (void *dst, const void *src, size_t n);
void  *memmove   (void *dst, const void *src, size_t n);
int    memcmp    (const void *s1, const void *s2, size_t n);
size_t strlen    (const char *s);
char  *strcat    (char *dst, const char *src);
char  *strcpy    (char *dst, const char *src);
char  *strncpy   (char *dst, const char *src, size_t n);
int    strcmp    (const char *s1, const char *s2);
int    strncmp   (const char *s1, const char *s2, size_t n);
#endif

// stdlib.h
#ifdef STDLIB
void   srand     (unsigned int seed);
int    rand      (void);
void  *malloc    (size_t size);
void   free      (void *ptr);
int    abs       (int x);
int    atoi      (const char *nptr);
#endif
// stdio.h
#ifdef STDIO
int    printf    (const char *format, ...);
int    sprintf   (char *str, const char *format, ...);
int    snprintf  (char *str, size_t size, const char *format, ...);
int    vsprintf  (char *str, const char *format, va_list ap);
int    vsnprintf (char *str, size_t size, const char *format, va_list ap);
int    putchar   (char c);
// void   _blockingputs(const char *);
// void   _nonblockingputs(const char *);
#endif
// assert.h
#ifdef NDEBUG
  #define assert(ignore) ((void)0)
#else
  #define assert(cond) \
    do { \
      if (!(cond)) { \
        printf("Assertion fail at %s:%d\n", __FILE__, __LINE__); \
        halt(1); \
      } \
    } while (0)
#endif

int __cxa_atexit(void (*func)(void*), void* arg, void* dso);


}
#endif

#endif