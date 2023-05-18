#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"
#include "resmgr.hh"
#include "klib.hh"

namespace fs{
    // using klib::SmartPtr;
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
            sharedptr<Pipe> pipe;
            sharedptr<INode> inode;
            Data(FileType type){
                switch(type){
                    case FileType::none:break;
                    case FileType::pipe:pipe=make_shared<Pipe>();break;
                    default:;
                }
            }
            Data(FileType type,const sharedptr<INode> &inode){
                assert(type==FileType::inode);
                this->inode=inode;
            }
            ~Data(){}
        }obj;
        File(FileType type):type(type),obj(type){
        }
        ~File(){ // 使用智能指针后，关闭逻辑在析构中处理
            switch(type){
                case FileType::pipe:obj.pipe.~sharedptr();break;
                case FileType::inode:
                    obj.inode.~sharedptr();
                        /*关闭逻辑*/
                    break;
            }
        }
        File(FileType a_type, const sharedptr<INode> &a_in): type(a_type), obj(type,a_in) {};
        void write(xlen_t addr,size_t len);
    };
}
#endif