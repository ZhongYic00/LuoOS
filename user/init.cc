#include "kernel.hh"
#include "linux/reboot.h"
struct Exec{
    const char *exec;
    const char **args;
    const char **envs;
};
int main(){
    register long *p asm("a0");
    // printf("init.elf\n");
    if(p){
        int argc = p[0];
	    char **argv = reinterpret_cast<char**>((void *)(p+1));
    }
    sys::syscall(sys::syscalls::testfatinit);
    // char path[]="clone";
    // char *execargv[]={ nullptr };
    // if(sys::syscall(sys::syscalls::clone)){
    //     sys::syscall2(sys::syscalls::execve,(xlen_t)path,(xlen_t)execargv);
    // }
    // sys::syscall(sys::syscalls::exit);
    // while(true){
    //     sys::syscall2(sys::syscalls::wait,-1,0);
    // }
    // 测例代码
    const char *envs[] = {"PATH=/","LD_LIBRARY_PATH=/",nullptr};
    const char *args0[]={"busybox","sh","lua_testcode.sh",nullptr};
    const char *args1[]={"busybox","sh","busybox_testcode.sh",nullptr};
    const char *args2[] = { "busybox","sh","-c","busybox grep -v -e tls_get -e sem_init -e pthread_cancel -e pthread_cond -e pthread_cond_ -e pthread_exit_cancel -e deadlock -e rwlock run-static.sh | while read line; do eval $line; done\n",nullptr };
    const char *args3[] = { "busybox","sh","-c","busybox grep -v -e tls_get -e sem_init -e pthread_cancel -e pthread_cond -e pthread_cond_ -e pthread_exit_cancel -e deadlock -e rwlock run-dynamic.sh  | while read line; do eval $line; done\n",nullptr };
    Exec testcases[]={
        {"busybox",args0,envs},
        {"busybox",args1,envs},
        {"busybox",args2,envs},
        {"busybox",args3,envs},
    };
    const int tsn = sizeof(testcases) / sizeof(Exec);
    // char *args[] = { "busybox","sh","run-static.sh",nullptr };
	for (int i = 0; i < tsn; ++i) {
		if (sys::syscall2(sys::syscalls::clone, 17, 0) == 0) {
            auto &test=testcases[i];
            sys::syscall3(sys::syscalls::execve, (xlen_t)test.exec, (xlen_t)test.args,(xlen_t)test.envs);
            // exec(testsuits[i], (char**)0);
        }
		else {
            sys::syscall2(sys::syscalls::wait,-1,0);
        }
	}
    sys::syscall3(sys::syscalls::reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_POWER_OFF);
	while(true);
	// exit(0);
}