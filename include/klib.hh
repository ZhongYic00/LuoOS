#ifndef KLIB_HH__
#define KLIB_HH__

#include "common.h"
#include "platform.h"
#include "klib.h"
#include "safestl.hh"
// #include "TINYSTL/string.h"
#include "new.hh"
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/list.h>

namespace klib
{
  using eastl::string;
  template<typename T>
  inline T min(T a,T b){ return a<b?a:b; }
  template<typename T>
  inline T max(T a,T b){ return (a>b?a:b); }
  inline int log2up(xlen_t a){
    int i=0,r=0;
    for(i=0;a>1;a>>=1,i++)r|=a&1;
    return i+r;
  }

  template<typename T1,typename T2>
  struct pair{
    T1 first;T2 second;
  };
  

  template<typename T,size_t buffsize=128>
  struct ringbuf
  {
    std::atomic<int> head,tail;
    T buff[128];
    ringbuf():head(0),tail(0){}
    inline void put(T d){
      buff[tail++%buffsize]=d;
    }
    inline T& req(){
      auto &rt=buff[tail++%buffsize];
      return rt;
    }
    inline void pop(){
      head++;
    }
    inline T get(){
      return buff[head%buffsize];
    }
    inline bool empty(){
      return head==tail;
    }
    inline bool full(){
      return head-tail==1;
    }
  };

  template<typename T>
  struct ListNode{
    T data;
    union {
      ListNode *prev;
      ListNode *next;
    }iter;
    ListNode(const T& data):data(data){this->iter.next=nullptr; }
  };

  namespace __format_internal
  {  
    template<typename T>
    inline decltype(auto) myForward(const T&& t){return t;}

    template<>
    inline decltype(auto) myForward(const string&& t){return t.c_str();}

    template<typename ...Ts>
    string format_(const char *fmt,Ts&& ...args){
      int size=snprintf(nullptr,-1,fmt,args...);
      assert(size>0);
      auto buf=new char[size+1];
      snprintf(buf,size,fmt,args...);
      auto rt=string(buf);
      delete[] buf;
      return rt;
    }
    
  } // namespace __format_interlal
  template<typename ...Ts>
  string format(const char *fmt,Ts&& ...args){
    return __format_internal::format_(fmt,__format_internal::myForward<Ts>(std::forward<Ts>(args))...);
  }
  template<typename T>
  string toString(const eastl::list<T> &l){
    string s="<list>[";
    for(auto &item:l)
      s+=item.toString()+",";
    s+="\t]\n";
    return s;
  }
  template<typename T>
  struct ArrayBuff{
    size_t len;
    T *buff;
    // @todo @bug lacks destructor
    ArrayBuff(size_t len):len(len){buff=new T[len];}
    ArrayBuff(T* addr,size_t len):ArrayBuff(len){
      memcpy(buff,addr,len*sizeof(T));
    }
    template<typename T1>
    ArrayBuff<T1> toArrayBuff(){
      return ArrayBuff<T1>{reinterpret_cast<T1*>(buff),len*(sizeof(T)/sizeof(T1))};
    }
    class iterator{
      T* ptr;
    public:
      iterator(T* p) : ptr(p) {}
      T& operator*() const {
        return *ptr;
      }
      T* operator->() const {
        return ptr;
      }
      iterator& operator++() {
        ++ptr;
        return *this;
      }
      iterator operator++(int) {
        iterator temp(*this);
        ++ptr;
        return temp;
      }
      iterator& operator--() {
        --ptr;
        return *this;
      }
      iterator operator--(int) {
        iterator temp(*this);
        --ptr;
        return temp;
      }
      iterator operator+(size_t n) const {
        return iterator(ptr + n);
      }
      iterator operator-(size_t n) const {
        return iterator(ptr - n);
      }
      iterator& operator+=(size_t n) {
        ptr += n;
        return *this;
      }
      iterator& operator-=(size_t n) {
        ptr -= n;
        return *this;
      }
      T& operator[](size_t n) const {
        return *(ptr + n);
      }
      bool operator==(const iterator& other) const {
        return ptr == other.ptr;
      }
      bool operator!=(const iterator& other) const {
        return ptr != other.ptr;
      }
    };
    iterator begin(){return iterator(buff);}
    iterator end(){return iterator(buff+len);}
    const char* c_str(){return reinterpret_cast<char*>(buff);}
  };
  typedef ArrayBuff<uint8_t> ByteArray;

  template<typename T>
  using SharedPtr=::eastl::shared_ptr<T>;
  /*
    SharedPtr, by Ct_Unvs
    SharedPtr只能用于动态对象，且SharedPtr本身不应使用new创建
  */
} // namespace klib

static klib::ringbuf<char> buf;

class IO{
public:
  static void _sbiputs(const char *s);
  static void _blockingputs(const char *s);
  inline static void _nonblockingputs(const char *s){
    _blockingputs("nblkpts");
    while(*s){
      while(buf.full());
      buf.put(*s++);
    }
        using namespace platform::uart0;
    for(char c;mmio<volatile lsr>(reg(LSR)).txidle && (c=IO::next())>0;)mmio<volatile uint8_t>(reg(THR))=c;
  }
  inline static char next(){
    if(buf.empty())return -1;
    char c=buf.get();buf.pop();return c;
  }
};

class Logger{
  struct LogItem{
    int id;
    char buf[300];
  };
  klib::ringbuf<LogItem> ring[5];
  std::atomic_uint32_t lid=0;
public:
  LogLevel outputLevel=LogLevel::info;
    void log(int level,const char *fmt,...);
};
extern Logger kLogger;

#endif