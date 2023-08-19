#ifndef SYS_HH__
#define SYS_HH__

#include "rvcsr.hh"

namespace sys
{
    enum syscalls{
        none,
        testexit,
        testyield,
        testwrite,
        testbio,
        testidle,
        testmount,
        testfatinit=7,
        getcwd=17,
        dup=23,
        dup3=24,
        fcntl=25,
        ioctl=29,
        mkdirat=34,
        unlinkat=35,
        symlinkat=36,
        linkat=37,
        umount2=39,
        mount=40,
        statfs=43,
        faccessat=48,
        chdir=49,
        fchdir=50,
        fchmod=52,
        fchmodat=53,
        fchownat=54,
        fchown=55,
        openat=56,
        close=57,
        pipe2=59,
        getdents64=61,
        lseek=62,
        read=63,
        write=64,
        readv=65,
        writev=66,
        sendfile=71,
        pselect=72,
        ppoll=73,
        readlinkat=78,
        fstatat=79,
        fstat=80,
        sync=81,
        exit=93,
        exit_group=94,
        settidaddress=96,
        futex=98,
        nanosleep=101,
        clock_gettime=113,
        yield=124,
        kill=129,
        tkill=130,
        sigaction=134,
        sigprocmask=135,
        sigreturn=139,
        reboot=142,
        setgid=144,
        setuid=146,
        getresuid=148,
        getresgid=150,
        setpgid=154,
        getpgid=155,
        getsid=156,
        setsid=157,
        getgroups=158,
        setgroups=159,
        times=153,
        uname=160,
        getrlimit=163,
        setrlimit=164,
        getrusage=165,
        umask=166,
        gettimeofday=169,
        getpid=172,
        getppid=173,
        getuid=174,
        geteuid=175,
        getgid=176,
        getegid=177,
        gettid=178,
        brk=214,
        munmap=215,
        mremap=216,
        clone=220,
        execve=221,
        mmap=222,
        mprotect=226,
        mlock=228,
        munlock=229,
        madvise=233,
        wait=260,
        syncfs=267,
        membarrier=283,
        nSyscalls,
    };
    enum statcode{
        ok=0, err=-1
    };
    static inline xlen_t syscall6(xlen_t id, xlen_t arg0, xlen_t arg1, xlen_t arg2, xlen_t arg3, xlen_t arg4, xlen_t arg5){
        register xlen_t a0 asm("a0") = arg0;
        register xlen_t a1 asm("a1") = arg1;
        register xlen_t a2 asm("a2") = arg2;
        register xlen_t a3 asm("a3") = arg3;
        register xlen_t a4 asm("a4") = arg4;
        register xlen_t a5 asm("a5") = arg5;
        register long a7 asm("a7") = id;
        asm volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"
                (a5), "r"(a7));
        return a0;
    }

    inline int syscall(int id){
        regWrite(a7,id);
        ExecInst(ecall);
        int a0;
        regRead(a0,a0);
        return a0;
    }
    inline int syscall1(int id,xlen_t arg0){
        return syscall6(id,arg0,0,0,0,0,0);
    }
    inline int syscall2(int id,xlen_t arg0,xlen_t arg1){
        return syscall6(id,arg0,arg1,0,0,0,0);
    }
    inline int syscall3(int id,xlen_t arg0,xlen_t arg1,xlen_t arg2){
        return syscall6(id,arg0,arg1,arg2,0,0,0);
    }
    
} // namespace sy
#endif