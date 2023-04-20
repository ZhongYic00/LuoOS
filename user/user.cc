#include "kernel.hh"

void program0(){
    while(true){
        sys::syscall(0);
        ExecInst(wfi);
    }
}
void program1(){
    while(true){
        sys::syscall(1);
        ExecInst(wfi);
    }
}