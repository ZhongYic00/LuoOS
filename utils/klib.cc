
#include "klib.hh"
#include "sbi.hh"
#include "lock.hh"

int enableLevel=LogLevel::info;

mutex::spinlock<false> putslock;
void IO::_sbiputs(const char *s){
    mutex::lock_guard guard(putslock);
    size_t len=strlen(s);  
    sbi_debug_console_write(len,(xlen_t)s,0);
}
void IO::_blockingputs(const char *s){
    mutex::lock_guard guard(putslock);
    while(*s)platform::uart0::blocking::putc(*s++);
}
Logger kLogger;
extern int _vsnprintf(char * out, size_t n, const char* s, va_list vl);
void Logger::log(int level,const char *fmt,...){
    auto &item=ring[level].req();
    item.id=++lid;
    va_list vl;
    va_start(vl,fmt);
    _vsnprintf(item.buf,500,fmt,vl);
    va_end(vl);
    if(level>=outputLevel)puts(item.buf);
}

void EASTL_DEBUG_BREAK(){ExecInst(ebreak);}
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line){
    return operator new(size);
}
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line){
    return operator new(size,std::align_val_t(alignment));
}