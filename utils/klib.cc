#ifndef GUEST

#include "klib.hh"
#include "alloc.hh"

void (*puts)(const char *s);

int putchar(char c){
	char buff[]={c,'\0'};
	puts(buff);
}

static int _vsnprintf(char * out, size_t n, const char* s, va_list vl)
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

static char out_buf[1000]; // buffer for _vprintf()

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

int printf(const char* s, ...)
{
	int res = 0;
	va_list vl;
	va_start(vl, s);
	res = _vprintf(s, vl);
	va_end(vl);
	return res;
}

void panic(char *s)
{
	printf("panic: ");
	printf(s);
	printf("\n");
	while(1){};
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

void *memset(void *s, int c, size_t n) {
  for(char *b=(char*)s;b-(char*)s<n;b++)*b=c;
  return s;
}

void *memmove(void *dst, const void *src, size_t n) {
  // may overlap
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
#endif

#endif
