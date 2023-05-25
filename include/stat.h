#ifndef __STAT_H
#define __STAT_H

#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device

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

namespace fs {
	struct dstat {
	  uint64 d_ino;	// 索引结点号
	  int64 d_off;	// 到下一个dirent的偏移
	  uint16 d_reclen;	// 当前dirent的长度
	  uint8 d_type;	// 文件类型
	  char d_name[STAT_MAX_NAME + 1];	//文件名
	};

	struct stat {
	  char name[STAT_MAX_NAME + 1]; // 文件名
	  int dev;     // File system's disk device // 文件系统的磁盘设备
	  short type;  // Type of file // 文件类型
	  uint64 size; // Size of file in bytes // 文件大小(字节)
	};

	struct kstat {
		dev_t st_dev;  			/* ID of device containing file */
		ino_t st_ino;  			/* Inode number */
		mode_t st_mode;  		/* File type and mode */
		nlink_t st_nlink;  		/* Number of hard links */
		uid_t st_uid;			/* User ID of owner */
		gid_t st_gid;			/* Group ID of owner */
		dev_t st_rdev;			/* Device ID (if special file) */
		unsigned long __pad;	
		off_t st_size;			/* Total size, in bytes */
		blksize_t st_blksize;	/* Block size for filesystem I/O */
		int __pad2; 			
		blkcnt_t st_blocks;		/* Number of 512B blocks allocated */
		long st_atime_sec;		/* Time of last access */
		long st_atime_nsec;		
		long st_mtime_sec;		/* Time of last modification */
		long st_mtime_nsec;
		long st_ctime_sec;		/* Time of last status change */
		long st_ctime_nsec;
		// unsigned __unused[2];
		unsigned unused[2]; // todo: 上面的写法在未实际使用的情况下过不了编译，最后要确定这个字段在我们的项目中是否有用，是否保留
	};
}

#endif