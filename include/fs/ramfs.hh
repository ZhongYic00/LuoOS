#ifndef RAMFS_HH__
#define RAMFS_HH__
#include "fcntl.h"
#include "fs.hh"

#define moduleLevel debug

namespace ramfs
{
    using fs::DEntry;
    using fs::INodeRef;
    using fs::memvec;
    using namespace klib;
    class FileSystem;
    class INode;
    class SuperBlock:public fs::SuperBlock{
        shared_ptr<INode> root;
        FileSystem* fs;
        ino_t ino=0;
    public:
        SuperBlock(FileSystem *);
        shared_ptr<fs::DEntry> getRoot() const override { return make_shared<fs::DEntry>(nullptr,"/",root); }
        shared_ptr<fs::DEntry> getMntPoint() const override { panic("bad design"); }
        shared_ptr<INode> mknod(bool isdir) {
            auto rt=eastl::make_shared<INode>(++ino,this,isdir);
            return rt;
        }
        template<typename T,typename... Args>
        inline shared_ptr<INode> mknod(Args&&... args){
            return make_shared<T>(++ino,this,args...);
        }
        fs::FileSystem *getFS() const override;
        bool isValid() const override { return true; }
        inline mode_t rDefaultMod() const override { return 0644; }
    };
    typedef shared_ptr<SuperBlock> SuperBlockRef;
    class INode:public fs::INode{
        SuperBlock *super;
        const ino_t ino;
        bool isDir;
        vector<uint8_t> bytes;
        unordered_map<string,shared_ptr<INode>> subs;
        fs::KStat kstat;
    public:
        INode(ino_t ino,SuperBlock *super,bool isdir=false):super(super),ino(ino),isDir(isdir){}
        ~INode(){
            Log(debug,"destruct");
        }

        // directory ops
        inline void link(string name,INodeRef nod) override{
            if(auto mynod=eastl::dynamic_pointer_cast<INode>(nod))
                subs[name]=mynod;
        }
        inline int nodHardUnlink() override {}
        inline INodeRef lookup(string a_dirname, uint *a_off = nullptr) override {
            if(!isDir) return nullptr;
            if(auto it=subs.find(a_dirname); it!=subs.end()){
                Log(debug,"found %s:%d",a_dirname.c_str(),it->second->ino);
                return it->second;
            }
            return nullptr;
        }
        inline int readDir(fs::DStat *a_buf, uint a_len, off_t &a_off) override {
            return 0;
        }
        inline INodeRef mknod(string a_name,mode_t mode) override {
            if(!isDir) return nullptr;
            bool isdir=S_ISDIR(mode);
            auto nod=super->mknod(isdir);
            subs[a_name]=nod;
            return nod;
        }
        inline int entSymLink(string a_target) override {
            return -1;
        }

        // file ops
        void nodRemove() override {}
        int chMod(mode_t a_mode) override {return -1;}
        int chOwn(uid_t a_owner, gid_t a_group) override {return -1;}
        void nodTrunc() override {return ;}
        virtual int nodRead(uint64 a_dst, uint a_off, uint a_len) override {
            if(isDir) return -1;
            auto rdbytes=klib::min(a_len,bytes.size()-a_off);
            memmove((ptr_t)a_dst,bytes.begin()+a_off,rdbytes);
            return rdbytes;
        }
        virtual int nodWrite(uint64 a_src, uint a_off, uint a_len) override {
            if(isDir) return -1;
            if(a_off+a_len>bytes.size())bytes.resize(a_off+a_len);
            memmove(bytes.begin()+a_off,(ptr_t)a_src,a_len);
            return a_len;
        }
        int readLink(char *a_buf, size_t a_bufsiz) override { return -1; }
        virtual mode_t rMode() const override {return isDir?S_IFDIR:S_IFREG;}
        dev_t rDev() const override {return 0;}
        off_t rFileSize() const override {return bytes.size();}
        ino_t rINo() const override {return ino;}
        bool isEmpty() {return isDir ? subs.size()==0 : bytes.size()==0;}
        fs::SuperBlockRef getSpBlk() const override { return super; }
        const timespec& rCTime() const {return timespec{kstat.st_atime_sec,kstat.st_atime_nsec};}
        const timespec& rMTime() const {return timespec{kstat.st_mtime_sec,kstat.st_mtime_nsec};}
        const timespec& rATime() const {return timespec{kstat.st_ctime_sec,kstat.st_ctime_nsec};}
    };
    // class Directory:public fs::Directory{
    //     typedef eastl::map<eastl::string,SharedPtr<INode>> IndexByName_t;
    //     IndexByName_t entries;
    // public:
    //     expected<INodeRef,Err> find(const eastl::string& name) override{
    //         return eastl::static_pointer_cast<fs::INode>(entries[name]);
    //     }
    //     expected<void,Err> link(const eastl::string& name, SharedPtr<fs::INode> INode) override{
    //         if(auto ramINode=eastl::dyn` amic_pointer_cast<INode>(INode)){
    //             entries[name]=ramINode;
    //             return expected<void,Err>();
    //         }
    //         return make_unexpected("dynamic cast error");
    //     }
    //     expected<void,Err> unlink(const Dentry &sub) override{
    //         /// @todo update self mtime/ctime, sub ctime
    //         entries.erase(sub.name);
    //         return expected<void,Err>();
    //     }
    // };
    class FileSystem:public fs::FileSystem{
        SuperBlockRef super;
    public:
        FileSystem(){
            super=make_shared<SuperBlock>(this);
            // root=eastl::make_shared<INode>(++ino);
        }
        // INodeRef getRoot() {return eastl::static_pointer_cast<fs::INode>(root);}
        string rFSType() const override {return "ramfs";}
        string rKey() const override {return 0;}
        bool isRootFS() const override {return false;}
        int ldSpBlk(uint64 a_dev, shared_ptr<fs::DEntry> a_mnt){panic("bad design");}
        shared_ptr<fs::SuperBlock> getSpBlk() const override {return super;}
        void unInstall() override {}
        long rMagic() const override {return 0x19270810;}
        long rBlkSiz() const override {return 512;}
        long rBlkNum() const override {return 0;}
        long rBlkFree() const override {return 0;}
        long rMaxFile() const override {return 0x1000;}
        long rFreeFile() const override {return 0;}
        long rNameLen() const override {return 512;}
    };
    inline SuperBlock::SuperBlock(FileSystem* fs):fs(fs){
        root=eastl::make_shared<INode>(++ino,this,true);
    }
    inline fs::FileSystem* SuperBlock::getFS() const { return fs; }
} // namespace ramfs

#endif