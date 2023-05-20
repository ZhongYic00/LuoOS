#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"
#include "resmgr.hh"
#include "klib.hh"

namespace fs{
    using klib::SharedPtr;
    // using klib::make_shared;
    struct INode{
        enum INodeType{
            dir, file, dev
        };
        bool m_valid;
        INodeType m_type;
        size_t m_size;
    };
    using pipe::Pipe;
    struct File{
        enum FileType{
            none,pipe,entry,dev,inode,
            stdin,stdout,stderr
        };
        const FileType type;
        enum class FileOp:uint16_t{
            none=0,read=0x1,write=0x2,append=0x4,
        };
        union FileOps{
            struct{
                int r:1;
                int w:1;
                int a:1;
                int gc:1;
                int defer:1;
            }fields;
            FileOp raw;
            FileOps(FileOp ops=FileOp::none):raw(ops){}
        }ops;
        union Data
        {
            SharedPtr<Pipe> pipe;
            SharedPtr<INode> inode;
            Data(FileType type){
                assert(type==none||type==stdin||type==stdout||type==stderr);
            }
            Data(const SharedPtr<Pipe> &pipe):pipe(pipe){}
            Data(const SharedPtr<INode> &inode):inode(inode){}
            ~Data(){}
        }obj;
        File(FileType type):type(type),obj(type){
        }
        File(const SharedPtr<Pipe> &pipe,FileOp ops):type(FileType::pipe),obj(pipe),ops(ops){}
        ~File(){ // 使用智能指针后，关闭逻辑在析构中处理
            switch(type){
                case FileType::pipe: {
                    // obj.pipe.~SharedPtr(); // 析构函数不应该被显式调用
                    obj.pipe.deRef();
                    break;
                }
                case FileType::inode: {
                    // obj.inode.~SharedPtr(); // 析构函数不应该被显式调用
                    obj.inode.deRef();
                    /*关闭逻辑*/
                    break;
                }
            }
        }
        File(FileType a_type, SharedPtr<INode> a_in): type(a_type), obj(a_in) {};
        void write(xlen_t addr,size_t len);
    };
}
#endif