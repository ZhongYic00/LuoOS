#ifndef SCATTERIO_HH__
#define SCATTERIO_HH__

#include "common.h"
#include "klib.hh"
#include "vm.hh"
#include "fs.hh"
#include "ipc.hh"

typedef klib::Segment<xlen_t> Slice;
using memvec=eastl::vector<Slice>;
class ScatteredIO{
public:
    virtual bool avail() const=0;
    virtual Slice next(size_t){panic("unimplemented!"); }
    virtual size_t contConsume(Slice){panic("unimplemented!");}
};

inline size_t scatteredCopy(ScatteredIO &dst,ScatteredIO &src){
    auto davail=dst.next(0);
    size_t tot=0,cnt;
    while(src.avail() && davail){
        davail=dst.next( cnt=src.contConsume(davail) );
        tot+=cnt;
    }
    return tot;
}

class KMemScatteredIO:public ScatteredIO{
    const memvec &vec;
    memvec::const_iterator it;
    xlen_t off;
public:
    KMemScatteredIO(const memvec &vec):vec(vec),it(vec.begin()),off(0){}
    bool avail() const override {return it!=vec.end();}
    Slice next(size_t bytes) override {
        off+=bytes;
        if(off==it->length()){off=0;it++;}
        return Slice{it->l+off,it->r};
    }
};

class UMemScatteredIO:public ScatteredIO{
    using memvec=eastl::vector<Slice>;
    vm::VMAR &vmar;
    const memvec &vec;
    memvec::const_iterator it;
    xlen_t off;
    unique_ptr<vm::VMOMapper> mapper;
    xlen_t bias;
    bool nextRegion(){
        using namespace vm;
        auto mp=vmar.findMapping(it->l);
        while(!mp&&it!=vec.end()){
            it++;
            mp=vmar.findMapping(it->l);
        }
        if(it!=vec.end()){
            /// @note this order is for kvmar not to first map overlapped then unmap
            mapper.reset();
            mapper=make_unique<VMOMapper>(mp->vmo);
            bias=it->l-pn2addr(mp->vpn)+pn2addr(mp->offset);
            return true;
        } else return false;
    }
public:
    UMemScatteredIO(vm::VMAR& vmar,const memvec &vec):vmar(vmar),vec(vec),it(vec.begin()),off(0){
        nextRegion();
    }
    bool avail() const override {return it!=vec.end();}
    Slice next(size_t bytes) override {
        using namespace vm;
        off+=bytes;
        if(off==it->length()){
            off=0;it++;
            if(!nextRegion())return Slice{1,0};
        }
        return Slice{mapper->start()+bias+off,mapper->start()+bias+it->length()-1};
    }
};

class SingleFileScatteredReader:public ScatteredIO{
    fs::INode &vnode;
    const memvec &vec;
    memvec::const_iterator it;
    xlen_t off;
public:
    SingleFileScatteredReader(fs::INode &node,const memvec &vec):vnode(node),vec(vec),it(vec.begin()),off(0){}
    bool avail() const override {return it!=vec.end();}
    size_t contConsume(Slice slice) override {
        auto availbytes=klib::min(it->length()-off,slice.length());
        auto rdbytes=vnode.nodRead(slice.l,it->l+off,availbytes);
        off+=rdbytes;
        if(off==it->length()){it++;off=0;}
        if(availbytes && !rdbytes)it=vec.end();
        return rdbytes;
    }
};
class SingleFileScatteredWriter:public ScatteredIO{
    fs::INode &vnode;
    const memvec &vec;
    memvec::const_iterator it;
    xlen_t off;
public:
    SingleFileScatteredWriter(fs::INode &node,const memvec &vec):vnode(node),vec(vec),it(vec.begin()),off(0){}
    bool avail() const override {return it!=vec.end();}
    size_t contConsume(Slice slice) override {
        auto availbytes=klib::min(it->length()-off,slice.length());
        auto rdbytes=vnode.nodWrite(slice.l,it->l+off,availbytes);
        assert(rdbytes>0);
        if(rdbytes<=0){it++;off=0;return 0;}
        off+=rdbytes;
        if(off==it->length()){it++;off=0;}
        if(availbytes && !rdbytes)it=vec.end();
        return rdbytes;
    }
};
class PrintScatteredWriter:public ScatteredIO{
public:
    bool avail() const override {return true;}
    size_t contConsume(Slice slice) override {
        auto availbytes=slice.length();
        auto str=reinterpret_cast<char*>(slice.l);
        for(int i=0;i<availbytes;i++)putchar(str[i]);
        return availbytes;
    }
};

class PipeScatteredWriter:public ScatteredIO{
    pipe::Pipe& pipe;
public:
    PipeScatteredWriter(pipe::Pipe& pipe_):pipe(pipe_){}
    bool avail() const override{return pipe.writable();}
    size_t contConsume(Slice slice) override {
        return pipe.write(ByteArray((uint8_t*)slice.l,slice.length()));
    }
};
#endif