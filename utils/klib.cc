
#include "klib.hh"
#include "sbi.hh"

void IO::_sbiputs(const char *s){
    size_t len=strlen(s);  
    sbi_debug_console_write(len,(xlen_t)s,0);
}
Logger kLogger;
extern int _vsnprintf(char * out, size_t n, const char* s, va_list vl);
void Logger::log(const char *fmt,...){
    auto &item=ring.req();
    item.id=++lid;
    va_list vl;
    va_start(vl,fmt);
    _vsnprintf(item.buf,300,fmt,vl);
    va_end(vl);
}

void EASTL_DEBUG_BREAK(){ExecInst(ebreak);}
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line){
    return operator new(size);
}
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line){
    panic("unimplemented!");
    return operator new(size);
}