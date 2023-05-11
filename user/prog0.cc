#include "kernel.hh"

int main(){
    while(true)
    {
        int i=10;
        char strbuf[]="process write test";
        sys::syscall3(sys::syscalls::testwrite,0,(xlen_t)strbuf,sizeof(strbuf));
        int pid=sys::syscall(sys::syscalls::getpid);
        char strbuf1[]="This is process[  ]";
        strbuf1[17]=pid+'0';
        sys::syscall3(sys::syscalls::testwrite,0,(xlen_t)strbuf1,sizeof(strbuf1));
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