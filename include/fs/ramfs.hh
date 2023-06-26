#ifndef RAMFS_HH__
#define RAMFS_HH__
#include "fs.hh"
#include "EASTL/vector.h"
#include "EASTL/map.h"
#include "EASTL/shared_ptr.h"

namespace ramfs
{
    using fs::internal::Dentry;
    using fs::internal::InodeRef;
    using namespace klib;
    class FileSystem;
    class Inode:public fs::internal::Inode{
        eastl::vector<uint8_t> bytes;
    public:
        Inode(ino_t ino):fs::internal::Inode(ino){}
        expected<klib::ByteArray,Err> read(size_t off,size_t len) override{
            size_t rdbytes=max(min(len,bytes.size()-off),0ul);
            // return make_expected(klib::ByteArray)
        }
        expected<xlen_t,Err> write(size_t off,klib::ByteArray bytes) override{
            return make_unexpected("unimplemented");
        }
    };
    class Directory:public fs::internal::Directory{
        typedef eastl::map<eastl::string,SharedPtr<Inode>> IndexByName_t;
        IndexByName_t entries;
    public:
        expected<InodeRef,Err> find(const eastl::string& name) override{
            return eastl::static_pointer_cast<fs::internal::Inode>(entries[name]);
        }
        expected<void,Err> link(const eastl::string& name, SharedPtr<fs::internal::Inode> inode) override{
            if(auto raminode=eastl::dynamic_pointer_cast<Inode>(inode)){
                entries[name]=raminode;
                return expected<void,Err>();
            }
            return make_unexpected("dynamic cast error");
        }
        expected<void,Err> unlink(const Dentry &sub) override{
            /// @todo update self mtime/ctime, sub ctime
            entries.erase(sub.name);
            return expected<void,Err>();
        }
    };
    class FileSystem:public fs::internal::FileSystem{
        SharedPtr<Inode> root;
        ino_t ino=0;
    public:
        FileSystem(){
            root=eastl::make_shared<Inode>(++ino);
        }
        InodeRef mknod() override{
            auto rt=eastl::make_shared<Inode>(++ino);
            return eastl::static_pointer_cast<fs::internal::Inode>(rt);
        }
        InodeRef getRoot() override{return eastl::static_pointer_cast<fs::internal::Inode>(root);}
    };
} // namespace ramfs

#endif