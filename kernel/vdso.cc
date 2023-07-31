#include "kernel.hh"

__attribute__((section("vDSO")))
__attribute__((naked))
void sigreturn(){
    sys::syscall(sys::syscalls::sigreturn);
}