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
template<typename T>class sharedptr;
typedef int ref_t;
template<typename T,typename... Ts>
class SharedObj{
    ref_t refs;
    SharedObj(Ts&& ...params):refs(0),obj(std::forward<Ts>(params)...){}
public:
    T obj;
    template<typename T1, typename... T1s>
    friend sharedptr<T1> make_shared(T1s&& ...params);
    // @todo atomic
    inline void ref(){refs++;}
    inline void release(){
        refs--;
        if(refs==0)delete this;
    }
    inline ref_t refcount(){return refs;}
};
template<typename T>
class sharedptr{
    SharedObj<T> *shared;
public:
    sharedptr()=delete;
    sharedptr(sharedptr<T>&& other)=default;
    sharedptr(SharedObj<T> *shared):shared(shared){shared->ref();}
    sharedptr(const sharedptr<T> &other):shared(other.shared){shared->ref();}
    ~sharedptr(){shared->release();}
    T &operator*() { return shared->obj; }
    T *operator->() { return &(shared->obj); }
    inline ref_t refcount(){return shared->refcount();}
    sharedptr<T>& operator=(const sharedptr<T>& other)
	{
        if(shared!=other.shared){
            shared->release();
            shared=other.shared;
            shared->ref();
        }
		return *this;
	}
};

template<typename T, typename... Ts>
sharedptr<T> make_shared(Ts&& ...params){
    auto obj=new SharedObj<T,Ts...>(std::forward<Ts>(params)...);
    return sharedptr<T>(obj);
}

#endif