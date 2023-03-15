#include "klib.hh"

extern "C" void start_kernel(){
    for(int i=0;i<10;i++)
        printf("%d:Hello RVOS!\n",i);
    halt();
}