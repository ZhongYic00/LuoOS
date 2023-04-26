#ifndef KLIB_HH__
#define KLIB_HH__

#include "common.h"
#include "platform.h"
#include "klib.h"

namespace klib
{
  template<typename T>
  inline T min(T a,T b){ return a<b?a:b; }
  template<typename T>
  inline T max(T a,T b){ return a>b?a:b; }
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
	T buff[128];
	size_t head,tail;
	FORCEDINLINE size_t next(size_t cur){return (cur+1)%buffsize;}
	inline void put(T d){
		buff[tail]=d;
		tail=next(tail);
	}
	inline void pop(){
		head=next(head);
	}
	inline T get(){
		return buff[head];
	}
	inline bool empty(){
		return head==tail;
	}
  inline bool full(){
    return next(tail)==head;
  }
};

template<typename T>
struct ListNode{
  T data;
  union {
    ListNode *prev;
    ListNode *next;
  }iter;
  ListNode(const T& data){ this->data=data;this->iter.next=nullptr; }
};
template<typename T>
struct list{
  typedef ListNode<T>* listndptr;
  listndptr head;
  listndptr tail;
  inline list(){ head=tail=nullptr; }
  static inline void insertAfter(listndptr cur,listndptr nd){
    nd->iter.next=cur->iter.next;
    cur->iter.next=nd;
  }
  static inline void insertBefore(listndptr cur,listndptr nd){
    nd->iter.next=cur;
  }

  inline void push_back(listndptr newNode){
    if(tail)
      list::insertAfter(tail,newNode),tail=newNode;
    else
      head=tail=newNode;
  }
  inline void push_back(const T &data){
    listndptr newNode=new ListNode<T>(data);
    push_back(newNode);
  }
  inline void push_front(listndptr newNode){
    if(head)
      list::insertBefore(head,newNode),head=newNode;
    else
      head=tail=newNode;
  }
  inline void push_front(const T &data){
    listndptr newNode=new ListNode<T>(data);
    push_front(newNode);
  }
  inline T pop_front(){
    T rt=head->data;
    if(head==tail)tail=nullptr;
    head=head->iter.next;
    delete head;
    return rt;
  }
  inline bool empty(){
    return head==nullptr;
  }
  inline void print(void (*printhook)(const T&)){
    printf("{head=0x%lx, tail=0x%lx} [\t",head,tail);
    for(listndptr cur=head;cur;cur=cur->iter.next)
      printhook(cur->data);
    // printf("\t]\n");
  }
};

} // namespace klib

static klib::ringbuf<char> buf;

class IO{
public:
  inline static void _blockingputs(const char *s){
    while(*s)platform::uart0::blocking::putc(*s++);
  }
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

#endif