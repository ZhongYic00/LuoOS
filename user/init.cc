#include "kernel.hh"
int main(){
    sys::syscall(sys::syscalls::testidle);
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