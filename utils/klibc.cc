#ifndef GUEST

#include "klib.h"
#include "alloc.hh"

int outputLevel=LogLevel::debug;

void (*puts)(const char *s);

int putchar(char c){
	char buff[]={c,'\0'};
	puts(buff);
}

int _vsnprintf(char * out, size_t n, const char* s, va_list vl)
{
	int format = 0;
	int longarg = 0;
	size_t pos = 0;
	for (; *s; s++) {
		if (format) {
			switch(*s) {
			case 'l': {
				longarg = 1;
				break;
			}
			case 'p': {
				longarg = 1;
				if (out && pos < n) {
					out[pos] = '0';
				}
				pos++;
				if (out && pos < n) {
					out[pos] = 'x';
				}
				pos++;
			}
			case 'x': {
				long num = longarg ? va_arg(vl, long) : va_arg(vl, int);
				int hexdigits = 2*(longarg ? sizeof(long) : sizeof(int))-1;
				for(int i = hexdigits; i >= 0; i--) {
					int d = (num >> (4*i)) & 0xF;
					if (out && pos < n) {
						out[pos] = (d < 10 ? '0'+d : 'a'+d-10);
					}
					pos++;
				}
				longarg = 0;
				format = 0;
				break;
			}
			case 'd': {
				long num = longarg ? va_arg(vl, long) : va_arg(vl, int);
				if (num < 0) {
					num = -num;
					if (out && pos < n) {
						out[pos] = '-';
					}
					pos++;
				}
				long digits = 1;
				for (long nn = num; nn /= 10; digits++);
				for (int i = digits-1; i >= 0; i--) {
					if (out && pos + i < n) {
						out[pos + i] = '0' + (num % 10);
					}
					num /= 10;
				}
				pos += digits;
				longarg = 0;
				format = 0;
				break;
			}
			case 's': {
				const char* s2 = va_arg(vl, const char*);
				while (*s2) {
					if (out && pos < n) {
						out[pos] = *s2;
					}
					pos++;
					s2++;
				}
				longarg = 0;
				format = 0;
				break;
			}
			case 'c': {
				if (out && pos < n) {
					out[pos] = (char)va_arg(vl,int);
				}
				pos++;
				longarg = 0;
				format = 0;
				break;
			}
			default:
				panic("printf: unimplemented!");
				break;
			}
		} else if (*s == '%') {
			format = 1;
		} else {
			if (out && pos < n) {
				out[pos] = *s;
			}
			pos++;
		}
    	}
	if (out && pos < n) {
		out[pos] = 0;
	} else if (out && n) {
		out[n-1] = 0;
	}
	return pos;
}

static char out_buf[2000]; // buffer for _vprintf()

static int _vprintf(const char* s, va_list vl)
{
	int res = _vsnprintf(NULL, -1, s, vl);
	if (res+1 >= sizeof(out_buf)) {
		puts("error: output string size overflow\n");
		while(1) {}
	}
	_vsnprintf(out_buf, res + 1, s, vl);
	puts(out_buf);
	return res;
}

int snprintf(char *str, size_t size, const char *format, ...){
	va_list vl;
	va_start(vl,format);
	int res=_vsnprintf(str,size+1,format,vl);
	va_end(vl);
	return res;
}

int printf(const char* s, ...)
{
	int res = 0;
	va_list vl;
	va_start(vl, s);
	res = _vprintf(s, vl);
	va_end(vl);
	return res;
}

void panic(const char *s)
{
	printf("panic: %s",s);
	halt();
}
void assert_(bool cond,const char *s){
	if(!cond)panic(s);
}
void halt(int errno){
	while(1)asm("wfi");
}


size_t strlen(const char *s) {
  size_t len=0;
  while(*s)s++,len++;
  return len;
}

char *strcpy(char *dst, const char *src) {
  char *j;
  for(j=dst;*src;src++,j++)*j=*src;
  *j='\0';
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  char *j;
  size_t cnt=0;
  for(j=dst;*src&&cnt<n;src++,j++,cnt++)*j=*src;
  for(;cnt<n;j++,cnt++)*j='\0';
  return dst;
}

char *strcat(char *dst, const char *src) {
  char *j=dst;
  while(*j)j++;
  for(;*src;src++,j++)*j=*src;
  *j='\0';
  return dst;
}

int strcmp(const char *s1, const char *s2) {
  for(;*s1==*s2;s1++,s2++){
    if(!*s1)return 0;
  }
  return *s1<*s2?-1:1;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  size_t cnt=1;
  if(n==0)return 0;
  for(;*s1==*s2;s1++,s2++,cnt++){
    if(!*s1||cnt==n)return 0;
  }
  return *s1<*s2?-1:1;
}

int strncmpamb(const char *s1, const char *s2, size_t n) {
  size_t cnt=1;
  if(n==0)return 0;
  for(;(*s1==*s2)||((*s1>='a')&&(*s1<='z')&&((*s2-*s1)==('A'-'a')))||((*s1>='A')&&(*s1<='Z')&&((*s1-*s2)==('A'-'a')));s1++,s2++,cnt++){
    if(!*s1||cnt==n)return 0;
  }
  return *s1<*s2?-1:1;
}

char* strchr(const char *s, char c) {
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

// convert wide char string into uchar string 
void snstr(char *dst, wchar const *src, int len) {
  while (len -- && *src) {
    *dst++ = (uchar)(*src & 0xff);
    src ++;
  }
  while(len-- > 0)
    *dst++ = 0;
}

void *memset(void *s, int c, size_t n) {
  for(char *b=(char*)s;b-(char*)s<n;b++)*b=c;
  return s;
}

void *memmove(void *dst, const void *src, size_t n) {
  // may overlap
  if(n<=0)return dst;
  const char *i=(const char*)src;
  char *j=(char*)dst;
  for(i+=n-1,j+=n-1;i>=(const char*)src;i--,j--)*j=*i;
  return dst;
}

void *memcpy(void *out, const void *in, size_t n) {
  const char *i=(const char*)in;
  char *j=(char*)out;
  for(;j-((char*)out)<n;i++,j++)*j=*i;
  return out;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  size_t cnt=1;
  const unsigned char *i=(const unsigned char*)s1, *j=(const unsigned char*)s2;
  if(n==0)return 0;
  for(;*i==*j;i++,j++,cnt++){
    if(!*i||cnt>=n)return 0;
  }
  return *i<*j?-1:1;
}
void *
memchr (const void *src_void,
	int c,
	size_t length)
{
  const unsigned char *src = (const unsigned char *) src_void;
  unsigned char d = c;

  while (length--)
    {
      if (*src == d)
        return (void *) src;
      src++;
    }

  return NULL;
}

static FORCEDINLINE uint32_t
asuint (float f)
{
#if defined(__riscv_flen) && __riscv_flen >= 32
  uint32_t result;
  __asm__("fmv.x.w\t%0, %1" : "=r" (result) : "f" (f));
  return result;
#else
  union
  {
    float f;
    uint32_t i;
  } u = {f};
  return u.i;
#endif
}
static FORCEDINLINE float
asfloat (uint32_t i)
{
#if defined(__riscv_flen) && __riscv_flen >= 32
  float result;
  __asm__("fmv.w.x\t%0, %1" : "=f" (result) : "r" (i));
  return result;
#else
  union
  {
    uint32_t i;
    float f;
  } u = {i};
  return u.f;
#endif
}
#define GET_FLOAT_WORD(i,d) ((i) = asuint(d))
#define SET_FLOAT_WORD(d,i) ((d) = asfloat(i))

#ifdef _FLT_LARGEST_EXPONENT_IS_NORMAL
#define FLT_UWORD_IS_FINITE(x) 1
#define FLT_UWORD_IS_NAN(x) 0
#define FLT_UWORD_IS_INFINITE(x) 0
#define FLT_UWORD_MAX 0x7fffffff
#define FLT_UWORD_EXP_MAX 0x43010000
#define FLT_UWORD_LOG_MAX 0x42b2d4fc
#define FLT_UWORD_LOG_2MAX 0x42b437e0
#define HUGE ((float)0X1.FFFFFEP128)
#else
#define FLT_UWORD_IS_FINITE(x) ((x)<0x7f800000L)
#define FLT_UWORD_IS_NAN(x) ((x)>0x7f800000L)
#define FLT_UWORD_IS_INFINITE(x) ((x)==0x7f800000L)
#define FLT_UWORD_MAX 0x7f7fffffL
#define FLT_UWORD_EXP_MAX 0x43000000
#define FLT_UWORD_LOG_MAX 0x42b17217
#define FLT_UWORD_LOG_2MAX 0x42b2d4fc
#define HUGE ((float)3.40282346638528860e+38)
#endif
#define FLT_UWORD_HALF_MAX (FLT_UWORD_MAX-(1L<<23))
#define FLT_LARGEST_EXP (FLT_UWORD_MAX>>23)

#ifdef _FLT_NO_DENORMALS
#define FLT_UWORD_IS_ZERO(x) ((x)<0x00800000L)
#define FLT_UWORD_IS_SUBNORMAL(x) 0
#define FLT_UWORD_MIN 0x00800000
#define FLT_UWORD_EXP_MIN 0x42fc0000
#define FLT_UWORD_LOG_MIN 0x42aeac50
#define FLT_SMALLEST_EXP 1
#else
#define FLT_UWORD_IS_ZERO(x) ((x)==0)
#define FLT_UWORD_IS_SUBNORMAL(x) ((x)<0x00800000L)
#define FLT_UWORD_MIN 0x00000001
#define FLT_UWORD_EXP_MIN 0x43160000
#define FLT_UWORD_LOG_MIN 0x42cff1b5
#define FLT_SMALLEST_EXP -22
#endif

float
ceilf(float x)
{
    __int32_t i0, j0;
    __uint32_t i, ix;
    GET_FLOAT_WORD(i0, x);
    ix = (i0 & 0x7fffffff);
    j0 = (ix >> 23) - 0x7f;
    if (j0 < 23) {
        if (j0 < 0) {
            if (i0 < 0) {
                i0 = 0x80000000;
            } else if (!FLT_UWORD_IS_ZERO(ix)) {
                i0 = 0x3f800000;
            }
        } else {
            i = (0x007fffff) >> j0;
            if ((i0 & i) == 0)
                return x; /* x is integral */
            if (i0 > 0)
                i0 += (0x00800000) >> j0;
            i0 &= (~i);
        }
    } else {
        if (!FLT_UWORD_IS_FINITE(ix))
            return x + x; /* inf or NaN */
        else
            return x; /* x is integral */
    }
    SET_FLOAT_WORD(x, i0);
    return x;
}


#ifdef ALLOC_HH__
extern "C" int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
// 	atexit_entry* entry = new atexit_entry;
// if (entry == NULL) return -1;
// entry->func = func;
// entry->arg = arg;
// entry->next = head;
// head = entry;
return 0;
}
extern "C" int __dso_handle(){
	panic("unimplemented!");
}
#endif

#endif
