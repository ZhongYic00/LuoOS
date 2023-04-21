#include "kernel.hh"

int main(){
    int i=10;
    while(i--){
        sys::syscall(0);
    }
    if(sys::syscall(sys::syscalls::testexit)==-1){
        ExecInst(wfi);
    }
    return i;
}