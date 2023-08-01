# RamFS

在VFS的架构下，我们实现了RamFS来管理一些特殊的系统文件和目录。这些文件和目录不存在于具体的磁盘上，其内容也由操作系统动态生成并维护。尽管设计允许，但大多数的修改操作在这些文件上没有意义。在文件系统层次中，它们最低只有INode层的对象，而没有disk层的操作及信息，且通常每个文件都有继承自INode的专有类型，通过重载对应操作的函数接口来实现访问这些文件时的特殊效果。

## procfs

/proc目录下挂载了一个基于RamFS的procfs文件系统，目前包含mounts、mountinfo、exe、meminfo文件，用于某些系统调用的访问。其中mounts和mountinfo保存了系统的所有挂载信息，exe则保存了当前可执行文件的符号链接，meminfo保存了系统的内存使用情况，可以由busybox的free命令使用。
```c
class MountsFile:public ramfs::INode{
    public:
        MountsFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
        int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
            ...
        }
};
```
```c
class MountsInfoFile:public ramfs::INode{
    public:
        MountsInfoFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
        int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
            ...
        }
};
```
```c
class ExeFile:public ramfs::INode{
    public:
        ExeFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
        int readLink(char *a_buf, size_t a_bufsiz) override {
            ...
        }
};
```
```c
class MemInfoFile:public ramfs::INode{
    public:
        MemInfoFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
        int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
            ...
        }
};
```

## devfs

/dev目录下挂载了一个同样基于RamFS的devfs文件系统，目前包含rtc、zero和null设备文件，其中rtc可以获取硬件时钟，zero可以输出任意长的0数据串并丢弃任何输入，null在读取时返回EOF并丢弃任何输入。
```c
class RTCFile:public ramfs::INode{
    public:
        RTCFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
        int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
            ...
        }
        mode_t rMode() const override {return S_IFCHR;}
        int ioctl(uint64_t req,addr_t arg) override{
            ...
        }
};
```
```c
class ZeroFile:public ramfs::INode{
    public:
        ZeroFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
        int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override {
            memset((void*)addr, 0, len);
            return len;
        }
        int nodWrite(uint64 a_src, uint a_off, uint a_len) override { return a_len; }
};
```
```c
class NullFile:public ramfs::INode{
    public:
        NullFile(ino_t ino,ramfs::SuperBlock *super):INode(ino,super){}
        int nodRead(addr_t addr, uint32_t uoff, uint32_t len) override { return -1; }  // EOF
        int nodWrite(uint64 a_src, uint a_off, uint a_len) override { return a_len; }
};
```