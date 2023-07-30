#ifndef __FCNTL_H
#define __FCNTL_H


#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device

// #define O_RDONLY    0x000    // 只读
// #define O_WRONLY    0x001    // 只写
// #define O_RDWR      0x002    // 读写
// #define O_CREATE    0x040    // 文件若不存在则新建
// #define O_TRUNC     0x200    // 当以只读或读写方式打开时,则将文件长度截断为0
// #define O_APPEND    0x400    //追加
// #define O_DIRECTORY 0x010000 // 如果参数path所指的文件并非目录, 则打开文件失败
// #define O_CLOEXEC   0x80000  // 当调用exec()函数成功后，文件描述符会自动关闭。

#define AT_FDCWD        -100
#define AT_REMOVEDIR    0x200

// #define FD_CLOEXEC  1

// #define F_DUPFD         1

#define FAT32_MAX_FILENAME  255
#define FAT32_MAX_PATH      260

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

/*
 * FMODE_EXEC is 0x20
 * FMODE_NONOTIFY is 0x4000000
 * These cannot be used by userspace O_* until internal and external open
 * flags are split.
 * -Eric Paris
 */

/*
 * When introducing new O_* bits, please check its uniqueness in fcntl_init().
 */

#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#ifndef O_CREAT
#define O_CREAT		00000100	/* not fcntl */
#endif
#ifndef O_EXCL
#define O_EXCL		00000200	/* not fcntl */
#endif
#ifndef O_NOCTTY
#define O_NOCTTY	00000400	/* not fcntl */
#endif
#ifndef O_TRUNC
#define O_TRUNC		00001000	/* not fcntl */
#endif
#ifndef O_APPEND
#define O_APPEND	00002000
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK	00004000
#endif
#ifndef O_DSYNC
#define O_DSYNC		00010000	/* used to be O_SYNC, see below */
#endif
#ifndef FASYNC
#define FASYNC		00020000	/* fcntl, for BSD compatibility */
#endif
#ifndef O_ASYNC
#define O_ASYNC		00020000	/* fcntl, for BSD compatibility */
#endif
#ifndef O_DIRECT
#define O_DIRECT	00040000	/* direct disk access hint */
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE	00100000
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY	00200000	/* must be a directory */
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW	00400000	/* don't follow links */
#endif
#ifndef O_NOATIME
#define O_NOATIME	01000000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC	02000000	/* set close_on_exec */
#endif

/*
 * Before Linux 2.6.33 only O_DSYNC semantics were implemented, but using
 * the O_SYNC flag.  We continue to use the existing numerical value
 * for O_DSYNC semantics now, but using the correct symbolic name for it.
 * This new value is used to request true Posix O_SYNC semantics.  It is
 * defined in this strange way to make sure applications compiled against
 * new headers get at least O_DSYNC semantics on older kernels.
 *
 * This has the nice side-effect that we can simply test for O_DSYNC
 * wherever we do not care if O_DSYNC or O_SYNC is used.
 *
 * Note: __O_SYNC must never be used directly.
 */
#ifndef O_SYNC
#define __O_SYNC	04000000
#define O_SYNC		(__O_SYNC|O_DSYNC)
#endif

#ifndef O_PATH
#define O_PATH		010000000
#endif

#ifndef __O_TMPFILE
#define __O_TMPFILE	020000000
#endif

/* a horrid kludge trying to make sure that this will fail on old kernels */
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#define O_TMPFILE_MASK (__O_TMPFILE | O_DIRECTORY | O_CREAT)      

#ifndef O_NDELAY
#define O_NDELAY	O_NONBLOCK
#endif

#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get close_on_exec */
#define F_SETFD		2	/* set/clear close_on_exec */
#define F_GETFL		3	/* get file->f_flags */
#define F_SETFL		4	/* set file->f_flags */
#ifndef F_GETLK
#define F_GETLK		5
#define F_SETLK		6
#define F_SETLKW	7
#endif
#ifndef F_SETOWN
#define F_SETOWN	8	/* for sockets. */
#define F_GETOWN	9	/* for sockets. */
#endif
#ifndef F_SETSIG
#define F_SETSIG	10	/* for sockets. */
#define F_GETSIG	11	/* for sockets. */
#endif

#ifndef CONFIG_64BIT
#ifndef F_GETLK64
#define F_GETLK64	12	/*  using 'struct flock64' */
#define F_SETLK64	13
#define F_SETLKW64	14
#endif
#endif

#ifndef F_SETOWN_EX
#define F_SETOWN_EX	15
#define F_GETOWN_EX	16
#endif

#ifndef F_GETOWNER_UIDS
#define F_GETOWNER_UIDS	17
#endif

/*
 * Open File Description Locks
 *
 * Usually record locks held by a process are released on *any* close and are
 * not inherited across a fork().
 *
 * These cmd values will set locks that conflict with process-associated
 * record  locks, but are "owned" by the open file description, not the
 * process. This means that they are inherited across fork() like BSD (flock)
 * locks, and they are only released automatically when the last reference to
 * the the open file against which they were acquired is put.
 */
#define F_OFD_GETLK	36
#define F_OFD_SETLK	37
#define F_OFD_SETLKW	38

#define F_OWNER_TID	0
#define F_OWNER_PID	1
#define F_OWNER_PGRP	2

/* for F_[GET|SET]FL */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */
#define F_DUPFD_CLOEXEC 1030

/* for posix fcntl() and lockf() */
#ifndef F_RDLCK
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2
#endif

/* for old implementation of bsd flock () */
#ifndef F_EXLCK
#define F_EXLCK		4	/* or 3 */
#define F_SHLCK		8	/* or 4 */
#endif

/* operations for bsd flock(), also used by the kernel implementation */
#define LOCK_SH		1	/* shared lock */
#define LOCK_EX		2	/* exclusive lock */
#define LOCK_NB		4	/* or'd with one of the above to prevent
				   blocking */
#define LOCK_UN		8	/* remove lock */

#define LOCK_MAND	32	/* This is a mandatory flock ... */
#define LOCK_READ	64	/* which allows concurrent read operations */
#define LOCK_WRITE	128	/* which allows concurrent write operations */
#define LOCK_RW		192	/* which allows concurrent read & write ops */

#define F_LINUX_SPECIFIC_BASE	1024

#endif