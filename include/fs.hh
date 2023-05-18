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
        union Data
        {
            SharedPtr<Pipe> pipe;
            SharedPtr<INode> inode;
            Data(FileType type){
                switch(type){
                    case FileType::none:
                        break;
                    case FileType::pipe: {
                        pipe = new Pipe;
                        break;
                    }
                    default:
                        break;
                }
            }
            Data(FileType type,SharedPtr<INode> inode){
                assert(type==FileType::inode);
                this->inode=inode;
            }
            ~Data(){}
        }obj;
        File(FileType type):type(type),obj(type){
        }
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
        File(FileType a_type, SharedPtr<INode> a_in): type(a_type), obj(type, a_in) {};
        void write(xlen_t addr,size_t len);
    };
}
#endif