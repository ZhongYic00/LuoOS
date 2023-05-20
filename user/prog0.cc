#include "kernel.hh"
void write(int fd,xlen_t addr,size_t len){
    sys::syscall3(sys::syscalls::write,fd,addr,len);
}
void print(const char* str){
    write(1,(xlen_t)str,strlen(str)+1);
}

int main(){
    puts=print;
    bool ischild;
    int pipe[2];
    sys::syscall2(sys::syscalls::pipe2,(xlen_t)pipe,0);
    printf("pipe2 result: rfd=%d wfd=%d\n",pipe[0],pipe[1]);
    if(sys::syscall(sys::syscalls::clone)==0){
        printf("parent return to here");
        ischild=false;
        char str[500]="test pipe write";
        write(pipe[1],(xlen_t)str,sizeof(str));
       }else{
        printf("child return to here");
        ischild=true;
        char readbuf[30];
    }
    if(sys::syscall(sys::syscalls::clone)==0){
        printf("parent of %s return to here",!ischild?"parent":"child");
        if(!ischild){
        }
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