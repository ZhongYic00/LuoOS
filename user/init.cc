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
    char path[]="clone";
    char *execargv[]={ nullptr };
    if(sys::syscall(sys::syscalls::clone)){
        sys::syscall2(sys::syscalls::execve,(xlen_t)path,(xlen_t)execargv);
    }
    sys::syscall(sys::syscalls::exit);
    // while(true){
    //     sys::syscall2(sys::syscalls::wait,-1,0);
    // }
    // 测例代码
    char *testsuits[] = {
        // "brk",
        "chdir",
        // "clone", 失败
        "close",
        "dup",
        "dup2",
        // "execve", 失败
        "exit",
        "fork",
        "fstat",
        "getcwd",
        "getdents",
        "getpid",
        // "getppid", 失败
        "gettimeofday",
        "mkdir_",
        // "mmap",
        "mount",
        // "munmap",
        "open",
        "openat",
        // "pipe", 失败
        "read",
        "sleep",
        "times",
        "umount",
        "uname",
        "unlink",
        "wait",
        // "waitpid", 失败
        "write",
        // "yield" 失败
    };
    const int tsn = sizeof(testsuits) / sizeof(char const*);
    char *args[] = { nullptr };
	for (int i = 0; i < tsn; ++i) {
		if (sys::syscall2(sys::syscalls::clone, 17, 0) == 0) {
            sys::syscall2(sys::syscalls::execve, (xlen_t)testsuits[i], (xlen_t)args);
            // exec(testsuits[i], (char**)0);
        }
		else {
            sys::syscall2(sys::syscalls::wait,-1,0);
        }
	}
	while(true);
	// exit(0);
}