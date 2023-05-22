
#include "klib.hh"
#include "sbi.hh"

void IO::_sbiputs(const char *s){
    size_t len=strlen(s);  
    sbi_debug_console_write(len,(xlen_t)s,0);
}