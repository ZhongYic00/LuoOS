# VFS

为了满足不同文件系统的需要（如ramfs），我们在初赛原有fat32的基础上增加了vfs层，作为上层操作文件系统的同一接口。不同的底层文件系统继承实现VFS的统一接口，并通过C++的虚函数机制实现不同文件系统的自然调用。

## 基础设计
超级块结构，设计上负责保存文件系统的元信息，但也允许底层文件系统自由实现，在VFS中为抽象类：
```c
class SuperBlock {
    public:
        SuperBlock() = default;
        SuperBlock(const SuperBlock& a_spblk) = default;
        virtual ~SuperBlock() = default;
        SuperBlock& operator=(const SuperBlock& a_spblk);
        virtual shared_ptr<DEntry> getRoot() const = 0;
        virtual shared_ptr<DEntry> getMntPoint() const = 0;
        virtual FileSystem *getFS() const = 0;
        virtual bool isValid() const = 0;
        virtual mode_t rDefaultMod() const = 0;
};
```
文件系统结构，为挂载时的直接操作对象，在VFS中为抽象类：
```c
class FileSystem {
     public:
        FileSystem() = default;
        FileSystem(const FileSystem& a_fs) = default;
        virtual ~FileSystem() = default;
        FileSystem& operator=(const FileSystem& a_fs) = default;
        virtual string rFSType() const = 0;
        virtual string rKey() const = 0;
        virtual bool isRootFS() const = 0;
        virtual shared_ptr<SuperBlock> getSpBlk() const = 0;
        virtual int ldSpBlk(uint64 a_dev, shared_ptr<fs::DEntry> a_mnt) = 0;
        virtual void unInstall() = 0;
        virtual long rMagic() const = 0;
        virtual long rBlkSiz() const = 0;
        virtual long rBlkNum() const = 0;
        virtual long rBlkFree() const = 0;
        virtual long rMaxFile() const = 0;
        virtual long rFreeFile() const = 0;
        virtual long rNameLen() const = 0;
};
```
索引节点结构，设计上负责保存文件在硬件设备上的索引信息，同样允许底层文件系统自由实现，在VFS中为抽象类：
```c
class INode {
    public:
        weak_ptr<vm::VMO> vmo;
        INode() = default;
        INode(const INode& a_inode) = default;
        virtual ~INode() = default;
        INode& operator=(const INode& a_inode) = default;
        virtual void link(string name,INodeRef nod) { panic("unsupported!"); }
        virtual INodeRef lookup(string a_dirname, uint *a_off = nullptr) = 0;
        virtual INodeRef mknod(string a_name,mode_t attr)=0;
        virtual void nodRemove() = 0;
        virtual int chMod(mode_t a_mode) = 0;
        virtual int chOwn(uid_t a_owner, gid_t a_group) = 0;
        void readv(const memvec &src,const memvec &dst);
        void readPages(const memvec &src,const memvec &dst);
        virtual int readLink(char *a_buf, size_t a_bufsiz) = 0;
        virtual int readDir(DStat *a_buf, uint a_len, off_t &a_off) = 0;
        virtual int ioctl(uint64_t req,addr_t arg) {return -ENOTTY;}
        virtual mode_t rMode() const = 0;
        virtual dev_t rDev() const = 0;
        virtual off_t rFileSize() const = 0;
        virtual ino_t rINo() const = 0;
        ......
};
```
目录项结构，负责缓存文件系统的树状结构，因为与操作系统的缓冲区机制直接相关因此不需要底层文件系统实现：
```c
class DEntry {
        shared_ptr<DEntry> parent;
        shared_ptr<INode> nod;
        string name;
        unordered_map<string,weak_ptr<DEntry>> subs;
        bool isMount;
    public:
        DEntry() = delete;
        DEntry(const DEntry& a_entry) = delete;
        DEntry& operator=(const DEntry& a_entry) = delete;
        DEntry(DERef prnt,string name,INodeRef nod_):parent(prnt),nod(nod_),isMount(false) { assert(nod_.use_count()); }
        Result<DERef> entSearch(DERef self,string a_dirname, uint *a_off = nullptr);
        Result<DERef> entCreate(DERef self,string a_name, mode_t mode);
        inline void setMntPoint() { isMount = true; }
        inline void clearMnt() { isMount = false; }
        inline int readDir(ArrayBuff<DStat> &a_bufarr, off_t &a_off) { return nod->readDir(a_bufarr.buff, a_bufarr.len, a_off); }
        inline string rName() const { return name; }
        inline DERef getParent() { return parent; }
        inline INodeRef getINode() { return nod; }
        inline bool isMntPoint() const { return isMount; }
        bool isRoot() const { return !parent; }
};
```
以上为VFS的核心部分。但作为操作系统的“主文件系统”，还需要提供面向用户的File层和Path层，为此我们进行了进一步的封装。

## File/Path层

文件结构方面，除与该文件对应的目录项外，还需要保存与打开方式相关的各类信息（即“打开时”信息）。区别于传统的将各种信息存一起的方式，我们采用了union的结构，仅保存该文件类型需要用到的信息，并自动进行类型检查。
```c
struct File {
    FileOps ops;
    int flags;
    union Data {
        private:
            struct { const FileType objtype; KStat kst; }dv;
            struct { const FileType objtype; KStat kst; shared_ptr<Pipe> pipe; }pp;
            struct { const FileType objtype; KStat kst; shared_ptr<DEntry> de; off_t off; }ep;
            inline void ensureDev() const { assert(dv.objtype==none || dv.objtype==stdin || dv.objtype==stdout || dv.objtype==stderr || dv.objtype==dev); }
            inline void ensurePipe() const { assert(pp.objtype == pipe); }
            inline void ensureEntry() const { assert(ep.objtype == entry); }
        public:
            Data(FileType a_type):dv({ a_type, a_type }) { ensureDev(); }
            Data(const shared_ptr<Pipe> &a_pipe): pp({ FileType::pipe, a_pipe, a_pipe }) {}
            Data(shared_ptr<DEntry> a_de): ep({ FileType::entry, a_de, a_de, 0 }) {}
            ~Data() {}
            inline shared_ptr<Pipe> getPipe() const { ensurePipe(); return pp.pipe; }
            inline shared_ptr<DEntry> getEntry() const { ensureEntry(); return ep.de; }
            inline KStat& kst() { return ep.kst; }
            inline off_t& off() { ensureEntry(); return ep.off; }
            inline FileType rType() { return ep.objtype; }
    }obj;
    ......
};
```
路径处理方面，我们利用string把用户传入的const char[]类型字符数组封装为具有专用处理函数的Path类，大大减少了路径搜索的代码量。同时，在封装过程中自动对路径按'/'字符进行分割，使得Path的使用更加灵活便利。更进一步地，可以将所有与路径相关的需求，如基于fd的相对路径和挂载表路径前缀，一并封装，从而统一所有路径操作。
```c
class Path {
    private:
        string pathname;
        vector<string> dirname;
        shared_ptr<DEntry> base;
    public:
        Path() = default;
        Path(const Path& a_path) = default;
        Path(const string& a_str, shared_ptr<File> a_base):pathname(a_str), dirname(), base(a_base==nullptr ? nullptr : a_base->obj.getEntry()) { pathBuild(); }
        Path(const string& a_str, shared_ptr<DEntry> a_base):pathname(a_str), dirname(), base(a_base) { pathBuild(); }
        Path(const string& a_str):pathname(a_str), dirname(), base(nullptr) { pathBuild(); }
        Path(const char *a_str, shared_ptr<File> a_base):pathname(a_str), dirname(), base(a_base==nullptr ? nullptr : a_base->obj.getEntry()) { pathBuild(); }
        Path(const char *a_str, shared_ptr<DEntry> a_base):pathname(a_str), dirname(), base(a_base) { pathBuild(); }
        Path(const char *a_str):pathname(a_str), dirname(), base(nullptr) { pathBuild(); }
        Path(shared_ptr<File> a_base):pathname(), dirname(), base(a_base==nullptr ? nullptr : a_base->obj.getEntry()) { pathBuild(); }
        Path(shared_ptr<DEntry> a_base):pathname(), dirname(), base(a_base) { pathBuild(); }
        ~Path() = default;
        Path& operator=(const Path& a_path) = default;
        void pathBuild();
        string pathAbsolute() const;
        shared_ptr<DEntry> pathHitTable();
        shared_ptr<DEntry> pathSearch(bool a_parent = false);
        shared_ptr<DEntry> pathCreate(short a_type, int a_mode);
        int pathRemove();
        int pathHardLink(Path a_newpath);
        int pathHardUnlink();
        int pathMount(Path a_devpath, string a_fstype);
        int mount(shared_ptr<FileSystem> fs);
        int pathUnmount() const;
        int pathOpen(int a_flags, mode_t a_mode = S_IFREG);
        int pathSymLink(string a_target);
};
```

## 便利性与特色

通过对VFS的恰当设计，我们构建了一个以继承机制为核心的虚拟文件系统。区别于传统的设计，这种设计不需要繁复的switch分支判断，具体接口的调用交由编译器根据虚函数机制自动完成。同时，在VFS本身的各种结构定义上，我们尽量不直接声明成员变量，而是声明预期使用的这个变量的读写函数，通过读写函数操作变量。这样的好处是能给予底层文件系统最大程度的自由实现空间，提高兼容性。底层实现甚至不必须有这个变量，只要它们能够以自己的方式为VFS提供“成员变量”的读写接口即可。

同时，我们对Path的封装极大地缩减了代码量，将原本复杂难懂的字符数组处理过程简化为了含义明确可复用的string类操作，进而在此基础上方便地支持了形式统一、自然的相对路径和路径匹配操作。只需要接收一个字符数组以及可选的相对基目录，Path就能自动将路径分割为一个个目录名，并根据基目录和相对路径推导出绝对路径。