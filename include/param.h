#ifndef __PARAM_H
#define __PARAM_H

// todo: 把宏定义写成合适的形式，删去我们不用的宏定义，优化布局，同步修改引用的代码

#define NPROC        50  // maximum number of processes
#define NCPU          2  // maximum number of CPUs
#define NOFILE      101  // open files per process
#define NFILE       200  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define NBUFLIST     4
#define FSSIZE       1000  // size of file system in blocks
#define MAXPATH      260   // maximum file path name
#define INTERVAL     (390000000 / 100) // timer interrupt interval
#define BINPRM_BUF_SIZE  128  //可执行文件头大小





#define WNOHANG       0x1
#define WUNTRACED     0x2
#define WCONTINUED    0x4

#endif