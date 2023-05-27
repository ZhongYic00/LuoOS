#include "kernel.hh"
void write(int fd,xlen_t addr,size_t len){
    sys::syscall3(sys::syscalls::write,fd,addr,len);
}
void read(int fd,xlen_t addr,size_t len){
    sys::syscall3(sys::syscalls::read,fd,addr,len);
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
    if(sys::syscall(sys::syscalls::clone)!=0){
        printf("parent return to here");
        ischild=false;
        // int ret=sys::syscall1(sys::syscalls::wait,-1);
        // printf("wait ret=%d\n",ret);
    }else{
        printf("child return to here");
        ischild=true;
        // sys::syscall(sys::syscalls::exit);
    }
    if(sys::syscall(sys::syscalls::clone)==0){
        printf("parent of %s return to here",!ischild?"parent":"child");
        if(!ischild){
            char str[500]="test pipe write";
            for(int i=0;i<sizeof(str);i++)write(pipe[1],(xlen_t)str+i,1);
            printf("pipe write success!");
        } else {
            for(int i=0;i<6;i++){
                char readbuf[101];
                read(pipe[0],(xlen_t)readbuf,sizeof(readbuf));
                printf("pipe read '%s'",readbuf);
            }
            printf("pipe read success!");
        }
       }else{
        printf("child of %s return to here",!ischild?"parent":"child");
    }
    // while(true)
    // {
    //     int i=10;
    //     printf("process write test");
    //     int pid=sys::syscall(sys::syscalls::getpid);
    //     printf("This is process[%d]",pid);
    //     while(i--){
    //         sys::syscall(0);
    //     }
    //     if(sys::syscall(sys::syscalls::testexit)==-1){
    //         ExecInst(wfi);
    //     } else {
    //         sys::syscall(sys::syscalls::yield);
    //     }
    // }
    while(true);
    return 0;
}