#include "kernel.hh"
#include "linux/reboot.h"
int main(){
    register long *p asm("a0");
    // printf("init.elf\n");
    if(p){
        int argc = p[0];
	    char **argv = reinterpret_cast<char**>((void *)(p+1));
    }
    sys::syscall(sys::syscalls::testfatinit);
    char path[]="/clone";
    char *execargv[]={"abcd","efgh",nullptr};
    if(!sys::syscall(sys::syscalls::clone)){
        sys::syscall2(sys::syscalls::execve,(xlen_t)path,(xlen_t)execargv);
    }
    for(pid_t rt=0;rt!=-1;){
        rt=sys::syscall2(sys::syscalls::wait,-1,0);
    }
    sys::syscall3(sys::syscalls::reboot,LINUX_REBOOT_MAGIC1,LINUX_REBOOT_MAGIC2,LINUX_REBOOT_CMD_POWER_OFF);
    // 测例代码
    /*
        FAT32初始化
        建立/dev/vda2目录
    */
    // char *testsuits[] = {
    //     "brk",
    //     "chdir",
    //     "clone", 失败
    //     "close",
    //     "dup", 成功
    //     "dup2", 成功
    //     "execve",
    //     "exit", 成功
    //     "fork", 成功
    //     "fstat",
    //     "getcwd", 成功
    //     "getdents",
    //     "getpid", 成功
    //     "getppid", 失败
    //     "gettimeofday", 成功
    //     "mkdir_",
    //     "mmap",
    //     "mount",
    //     "munmap",
    //     "open",
    //     "openat",
    //     "pipe",
    //     "read",
    //     "sleep", 成功
    //     "times", 成功
    //     "umount",
    //     "uname", 成功
    //     "unlink",
    //     "wait", 成功
    //     "waitpid", 失败
    //     "write",
    //     "yield" 失败
    // };
    // const int tsn = sizeof(testsuits) / sizeof(char const*);
	// for (int i = 0; i < tsn; ++i) {
	// 	if (fork() == 0) { exec(testsuits[i], (char**)0); }
	// 	else { wait((void*)0); }
	// }
	// while(true);
	// exit(0);
}