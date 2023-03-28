#ifndef KLIB_H__
#define KLIB_H__

#include "types.h"
#include "platform.h"

template<typename T,size_t buffsize=128>
struct ringbuf
{
	T buff[128];
	size_t head,tail;
	FORCEDINLINE size_t next(size_t cur){return (cur+1)%buffsize;}
	void put(T d){
		buff[tail]=d;
		tail=next(tail);
	}
	void pop(){
		head=next(head);
	}
	T get(){
		return buff[head];
	}
	bool empty(){
		return head==tail;
	}
  bool full(){
    return next(tail)==head;
  }
};
static ringbuf<char> buf;

class IO{
public:
  inline static void _blockingputs(const char *s){
    while(*s)platform::uart0::blocking::putc(*s++);
  }
  inline static void _nonblockingputs(const char *s){
    _blockingputs("nblkpts");
    while(*s){
      while(buf.full());
      buf.put(*s++);
    }
        using namespace platform::uart0;
    for(char c;mmio<volatile lsr>(reg(LSR)).txidle && (c=IO::next())>0;)mmio<volatile uint8_t>(reg(THR))=c;
  }
  inline static char next(){
    if(buf.empty())return -1;
    char c=buf.get();buf.pop();return c;
  }
};
#define saveContext() __asm__("sd x1,0(t6)\n sd x2,8(t6)\n sd x3,16(t6)\n sd x4,24(t6)\n sd x5,32(t6)\n sd x6,40(t6)\n sd x7,48(t6)\n sd x8,56(t6)\n sd x9,64(t6)\n sd x10,72(t6)\n sd x11,80(t6)\n sd x12,88(t6)\n sd x13,96(t6)\n sd x14,104(t6)\n sd x15,112(t6)\n sd x16,120(t6)\n sd x17,128(t6)\n sd x18,136(t6)\n sd x19,144(t6)\n sd x20,152(t6)\n sd x21,160(t6)\n sd x22,168(t6)\n sd x23,176(t6)\n sd x24,184(t6)\n sd x25,192(t6)\n sd x26,200(t6)\n sd x27,208(t6)\n sd x28,216(t6)\n sd x29,224(t6)\n sd x30,232(t6)\n")
#define restoreContext() __asm__("ld x1,0(t6)\n ld x2,8(t6)\n ld x3,16(t6)\n ld x4,24(t6)\n ld x5,32(t6)\n ld x6,40(t6)\n ld x7,48(t6)\n ld x8,56(t6)\n ld x9,64(t6)\n ld x10,72(t6)\n ld x11,80(t6)\n ld x12,88(t6)\n ld x13,96(t6)\n ld x14,104(t6)\n ld x15,112(t6)\n ld x16,120(t6)\n ld x17,128(t6)\n ld x18,136(t6)\n ld x19,144(t6)\n ld x20,152(t6)\n ld x21,160(t6)\n ld x22,168(t6)\n ld x23,176(t6)\n ld x24,184(t6)\n ld x25,192(t6)\n ld x26,200(t6)\n ld x27,208(t6)\n ld x28,216(t6)\n ld x29,224(t6)\n ld x30,232(t6)\n")

extern "C" {
#define STDIO 1

//#define __NATIVE_USE_KLIB__

extern void (*puts)(const char *s);
void halt();
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
}
#endif
