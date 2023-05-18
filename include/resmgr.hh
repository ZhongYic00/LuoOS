#ifndef RESMGR_HH__
#define RESMGR_HH__

#include "common.h"
#include "safestl.hh"
#include "klib.hh"

typedef int tid_t;
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
    void free(tid_t id);
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
#endif