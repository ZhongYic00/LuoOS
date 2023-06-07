#ifndef __FCNTL_H
#define __FCNTL_H


#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device

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

#define ATTR_READ_ONLY      0x01  // 只读
#define ATTR_HIDDEN         0x02  // 隐藏
#define ATTR_SYSTEM         0x04  // 系统
#define ATTR_VOLUME_ID      0x08  // 卷标
#define ATTR_DIRECTORY      0x10  // 目录
#define ATTR_ARCHIVE        0x20  // 文档
#define ATTR_LONG_NAME      0x0F  // 长名
#define ATTR_LINK           0x40  // link

#define LAST_LONG_ENTRY     0x40  // 最后一个长文件名目录
#define FAT32_EOC           0x0ffffff8  // 
#define EMPTY_ENTRY         0xe5
#define END_OF_ENTRY        0x00
#define CHAR_LONG_NAME      13
#define CHAR_SHORT_NAME     11

#define FAT32_MAX_FILENAME  255
#define FAT32_MAX_PATH      260
#define ENTRY_CACHE_NUM     50

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

#define STAT_MAX_NAME 32

#define S_IFMT     0170000   // 文件类型bit字段的位掩码 
#define S_IFSOCK   0140000   // socket
#define S_IFLNK    0120000   // 符号链接
#define S_IFREG    0100000   // 普通文件
#define S_IFBLK    0060000   // 块设备
#define S_IFDIR    0040000   // 目录
#define S_IFCHR    0020000   // 字符设备
#define S_IFIFO    0010000   // FIFO

#define S_ISREG(m)    (((m) & S_IFMT) == S_IFREG) 
#define S_ISDIR(m)    (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)    (((m) & S_IFMT) == S_IFBLK)
#define S_ISLNK(m)    (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m)    (((m) & S_IFMT) == S_IFCHR)
#define S_ISIFO(m)    (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)   (((m) & S_IFMT) == S_IFSOCK)

#endif