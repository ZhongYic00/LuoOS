#include "kernel.hh"
int main(){
    register long *p asm("a0");
    if(p){
        int argc = p[0];
	    char **argv = reinterpret_cast<char**>((void *)(p+1));
    }
    char *execargv[]={"abcd","efgh",nullptr};
    if(sys::syscall(sys::syscalls::clone)){
        sys::syscall2(sys::syscalls::execve,0,(xlen_t)execargv);
    }
    sys::syscall(sys::syscalls::exit);
    while(true){
        sys::syscall2(sys::syscalls::wait,-1,0);
    }
    // 测例代码
    /*
        FAT32初始化
        建立/dev/vda2目录
    */
    // char *testsuits[] = {
    //     "brk",
    //     "chdir",
    //     "clone",
    //     "close",
    //     "dup",
    //     "dup2",
    //     "execve",
    //     "exit",
    //     "fork",
    //     "fstat",
    //     "getcwd",
    //     "getdents",
    //     "getpid",
    //     "getppid",
    //     "gettimeofday",
    //     "mkdir_",
    //     "mmap",
    //     "mount",
    //     "munmap",
    //     "open",
    //     "openat",
    //     "pipe",
    //     "read",
    //     "sleep",
    //     "test_echo",
    //     "times",
    //     "umount",
    //     "uname",
    //     "unlink",
    //     "wait",
    //     "waitpid",
    //     "write",
    //     "yield"
    // };
    // const int tsn = sizeof(testsuits) / sizeof(char const*);
	// for (int i = 0; i < tsn; ++i) {
	// 	if (fork() == 0) { exec(testsuits[i], (char**)0); }
	// 	else { wait((void*)0); }
	// }
	// while(true);
	// exit(0);
}