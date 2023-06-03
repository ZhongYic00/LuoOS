#include "kernel.hh"
int main(){
    if(sys::syscall(sys::syscalls::clone)){
        sys::syscall(sys::syscalls::execve);
    }
    sys::syscall(sys::syscalls::exit);
    while(true){
        sys::syscall2(sys::syscalls::wait,-1,0);
    }
}