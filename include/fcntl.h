#ifndef __FCNTL_H
#define __FCNTL_H


#define O_RDONLY    0x000    // 只读
#define O_WRONLY    0x001    // 只写
#define O_RDWR      0x002    // 读写
#define O_CREATE    0x040    // 文件若不存在则新建
#define O_TRUNC     0x200    // 当以只读或读写方式打开时,则将文件长度截断为0
#define O_APPEND    0x400    //追加
#define O_DIRECTORY 0x010000 // 如果参数path所指的文件并非目录, 则打开文件失败
#define O_CLOEXEC   0x80000  // 当调用exec()函数成功后，文件描述符会自动关闭。

#define AT_FDCWD        -100
#define AT_REMOVEDIR    0x200

#define FD_CLOEXEC  1

#define F_DUPFD         1
#define F_DUPFD_CLOEXEC 1030

#endif