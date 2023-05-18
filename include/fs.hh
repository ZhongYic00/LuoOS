#ifndef FS_HH__
#define FS_HH__

#include "common.h"
#include "ipc.hh"
#include "klib.hh"

namespace fs{
    using klib::SmartPtr;
    struct INode{
        enum INodeType{
            dir, file, dev
        };
        bool m_valid;
        INodeType m_type;
        size_t m_size;
    };
    struct File{
        enum FileType{
            none,pipe,entry,dev,inode,
            stdin,stdout,stderr
        };
        FileType type;
        SmartPtr<INode> in;
        union Data
        {
            pipe::Pipe* pipe;
        }obj;
        File(FileType a_type=none, SmartPtr<INode> a_in=nullptr): type(a_type), in(a_in) {};
        ~File(); // 使用智能指针后，关闭逻辑在析构中处理
        void write(xlen_t addr,size_t len);
    };
}
#endif