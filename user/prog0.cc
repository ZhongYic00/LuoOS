#include "kernel.hh"

int main(){
    while(true)
    {
        int i=10;
        sys::syscall3(sys::syscalls::testwrite,0,0,0);
        while(i--){
            sys::syscall(0);
        }
        if(sys::syscall(sys::syscalls::testexit)==-1){
            ExecInst(wfi);
        } else {
            sys::syscall(sys::syscalls::yield);
        }
    }
    return 0;
}