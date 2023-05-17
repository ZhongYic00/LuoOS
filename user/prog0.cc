#include "kernel.hh"

void print(const char* str){
    sys::syscall3(sys::syscalls::testwrite,0,(xlen_t)str,strlen(str)+1);
}

int main(){
    puts=print;
    bool ischild;
    if(sys::syscall(sys::syscalls::clone)==0){
        printf("parent return to here");
        ischild=false;
       }else{
        printf("child return to here");
        ischild=true;
    }
    if(sys::syscall(sys::syscalls::clone)==0){
        printf("parent of %s return to here",!ischild?"parent":"child");
       }else{
        printf("child of %s return to here",!ischild?"parent":"child");
    }
    while(true)
    {
        int i=10;
        printf("process write test");
        int pid=sys::syscall(sys::syscalls::getpid);
        printf("This is process[%d]",pid);
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