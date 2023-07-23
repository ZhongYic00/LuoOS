#ifndef RESMGR_HH__
#define RESMGR_HH__

#include "common.h"
#include "safestl.hh"
#include "klib.hh"

struct IdManagable{
    const tid_t id;
    IdManagable(tid_t id):id(id){}
    IdManagable(const IdManagable &other)=delete;
};
template<typename T>
class ObjManager{
    constexpr static int nobjs=128;
    int idCnt;
    T *objlist[nobjs];
public:
    inline tid_t newId(){return ++idCnt;}
    inline void addObj(tid_t id,T* obj){objlist[id]=obj;}
    inline int getObjNum() { return nobjs; }
    void del(T *obj);
    inline T* operator[](tid_t id){return objlist[id];}
};

template<typename T>
ptr_t operator new(size_t size, ObjManager<T> &mgr){
    ptr_t obj=operator new(size);
    auto id=mgr.newId();
    new (obj) IdManagable(id);
    mgr.addObj(id,(T*)obj);
    return obj;
}
template<typename T>
void ObjManager<T>::del(T* obj){
    obj->~T();
    objlist[obj->id]=nullptr;
    /// @todo id recycle
}
#endif