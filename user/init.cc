#include "kernel.hh"
int main(){
    sys::syscall(sys::syscalls::testidle);
    while(true){
        sys::syscall2(sys::syscalls::wait,-1,0);
    }
}