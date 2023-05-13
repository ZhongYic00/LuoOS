#ifndef KLIB_HH__
#define KLIB_HH__

#include "common.h"
#include "platform.h"
#include "klib.h"
#include "safestl.hh"

namespace klib
{
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
  ListNode(const T& data):data(data){this->iter.next=nullptr; }
};

template<typename T>
class Seq{};

template<typename T,bool LOOPBACK=false>
struct list:public Seq<T>{
  typedef ListNode<T>* listndptr;
  listndptr head;
  listndptr tail;
  inline list(){ head=tail=nullptr; }
  inline list(const std::initializer_list<T> &il):list(){
    for(const auto &i:il)push_back(i);
  }
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
  class iterator{
    listndptr ptr;
    list<T,LOOPBACK> *parent;
  public:
      iterator(listndptr p,list<T,LOOPBACK> *parent):ptr(p),parent(parent){}
      T& operator*() const {
          return ptr->data;
      }
      T* operator->() const {
          return &(ptr->data);
      }
      iterator& operator++() {
        if(LOOPBACK&&ptr==parent->tail)
          ptr=parent->head;
        else
          ptr=ptr->iter.next;
        return *this;
      }
      iterator operator++(int) {
          iterator temp(*this);
          if(LOOPBACK&&ptr==parent->tail)
            ptr=parent->head;
          else
            ptr=ptr->iter.next;
          return temp;
      }

      // iterator operator+(size_t n) const {
      //     listndptr temp=ptr;
      //     while(n-- && temp!=nullptr)temp=temp->iter.next;
      //     return iterator(temp);
      // }
      // iterator& operator+=(size_t n) {
      //     while(n-- && ptr!=nullptr)ptr=ptr->iter.next;
      //     return *this;
      // }
      bool operator==(const iterator& other) const {
          return ptr == other.ptr;
      }
      bool operator!=(const iterator& other) const {
          return ptr != other.ptr;
      }

    };
  iterator begin() {
      return iterator(head,this);
  }
  iterator end() {
      return iterator(tail,this);
  }
  inline bool empty(){
    return head==nullptr;
  }
  inline void remove(const T &data){
    // iterator prev;
    // for(auto i:*this){
    //   if(*i==data){
    //     prev.ptr->iter.next=i.ptr->iter.next;
    //     delete i->ptr;
    //   }
    //   prev=i;
    // }
  }
  inline void print(void (*printhook)(const T&)){
    printf("{head=0x%lx, tail=0x%lx} [\t",head,tail);
    for(listndptr cur=head;cur;cur=cur->iter.next)
      printhook(cur->data);
    // printf("\t]\n");
  }
};

  template<typename T>
  struct ArrayBuff{
    size_t len;
    T *buff;
    ArrayBuff(size_t len):len(len){buff=new T[len];}
    ArrayBuff(T* addr,size_t len):ArrayBuff(len){
        memcpy(buff,addr,len*sizeof(T));
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

// namespace klib
// {
//     template<typename _Tp, std::size_t _Nm>
//     struct __array_traits
//     {
//       typedef _Tp _Type[_Nm];
//       typedef __is_swappable<_Tp> _Is_swappable;
//       typedef __is_nothrow_swappable<_Tp> _Is_nothrow_swappable;

//       static constexpr _Tp&
//       _S_ref(const _Type& __t, std::size_t __n) noexcept
//       { return const_cast<_Tp&>(__t[__n]); }

//       static constexpr _Tp*
//       _S_ptr(const _Type& __t) noexcept
//       { return const_cast<_Tp*>(__t); }
//     };

//  template<typename _Tp>
//    struct __array_traits<_Tp, 0>
//    {
//      struct _Type { };
//      typedef true_type _Is_swappable;
//      typedef true_type _Is_nothrow_swappable;

//      static constexpr _Tp&
//      _S_ref(const _Type&, std::size_t) noexcept
//      { return *static_cast<_Tp*>(nullptr); }

//      static constexpr _Tp*
//      _S_ptr(const _Type&) noexcept
//      { return nullptr; }
//    };

//   /**
//    *  @brief A standard container for storing a fixed size sequence of elements.
//    *
//    *  @ingroup sequences
//    *
//    *  Meets the requirements of a <a href="tables.html#65">container</a>, a
//    *  <a href="tables.html#66">reversible container</a>, and a
//    *  <a href="tables.html#67">sequence</a>.
//    *
//    *  Sets support random access iterators.
//    *
//    *  @tparam  Tp  Type of element. Required to be a complete type.
//    *  @tparam  N  Number of elements.
//   */
//   template<typename _Tp, std::size_t _Nm>
//     struct array
//     {
//       typedef _Tp 	    			      value_type;
//       typedef value_type*			      pointer;
//       typedef const value_type*                       const_pointer;
//       typedef value_type&                   	      reference;
//       typedef const value_type&             	      const_reference;
//       typedef value_type*          		      iterator;
//       typedef const value_type*			      const_iterator;
//       typedef std::size_t                    	      size_type;
//       typedef std::ptrdiff_t                   	      difference_type;
//       typedef std::reverse_iterator<iterator>	      reverse_iterator;
//       typedef std::reverse_iterator<const_iterator>   const_reverse_iterator;

//       // Support for zero-sized arrays mandatory.
//       typedef _GLIBCXX_STD_C::__array_traits<_Tp, _Nm> _AT_Type;
//       typename _AT_Type::_Type                         _M_elems;

//       // No explicit construct/copy/destroy for aggregate type.

//       // DR 776.
//       void
//       fill(const value_type& __u)
//       { std::fill_n(begin(), size(), __u); }

//       void
//       swap(array& __other)
//       noexcept(_AT_Type::_Is_nothrow_swappable::value)
//       { std::swap_ranges(begin(), end(), __other.begin()); }

//       // Iterators.
//       _GLIBCXX17_CONSTEXPR iterator
//       begin() noexcept
//       { return iterator(data()); }

//       _GLIBCXX17_CONSTEXPR const_iterator
//       begin() const noexcept
//       { return const_iterator(data()); }

//       _GLIBCXX17_CONSTEXPR iterator
//       end() noexcept
//       { return iterator(data() + _Nm); }

//       _GLIBCXX17_CONSTEXPR const_iterator
//       end() const noexcept
//       { return const_iterator(data() + _Nm); }

//       _GLIBCXX17_CONSTEXPR reverse_iterator
//       rbegin() noexcept
//       { return reverse_iterator(end()); }

//       _GLIBCXX17_CONSTEXPR const_reverse_iterator
//       rbegin() const noexcept
//       { return const_reverse_iterator(end()); }

//       _GLIBCXX17_CONSTEXPR reverse_iterator
//       rend() noexcept
//       { return reverse_iterator(begin()); }

//       _GLIBCXX17_CONSTEXPR const_reverse_iterator
//       rend() const noexcept
//       { return const_reverse_iterator(begin()); }

//       _GLIBCXX17_CONSTEXPR const_iterator
//       cbegin() const noexcept
//       { return const_iterator(data()); }

//       _GLIBCXX17_CONSTEXPR const_iterator
//       cend() const noexcept
//       { return const_iterator(data() + _Nm); }

//       _GLIBCXX17_CONSTEXPR const_reverse_iterator
//       crbegin() const noexcept
//       { return const_reverse_iterator(end()); }

//       _GLIBCXX17_CONSTEXPR const_reverse_iterator
//       crend() const noexcept
//       { return const_reverse_iterator(begin()); }

//       // Capacity.
//       constexpr size_type
//       size() const noexcept { return _Nm; }

//       constexpr size_type
//       max_size() const noexcept { return _Nm; }

//       _GLIBCXX_NODISCARD constexpr bool
//       empty() const noexcept { return size() == 0; }

//       // Element access.
//       _GLIBCXX17_CONSTEXPR reference
//       operator[](size_type __n) noexcept
//       { return _AT_Type::_S_ref(_M_elems, __n); }

//       constexpr const_reference
//       operator[](size_type __n) const noexcept
//       { return _AT_Type::_S_ref(_M_elems, __n); }

//       _GLIBCXX17_CONSTEXPR reference
//       at(size_type __n)
//       {
// 	if (__n >= _Nm)
// 	  std::__throw_out_of_range_fmt(__N("array::at: __n (which is %zu) "
// 					    ">= _Nm (which is %zu)"),
// 					__n, _Nm);
// 	return _AT_Type::_S_ref(_M_elems, __n);
//       }

//       constexpr const_reference
//       at(size_type __n) const
//       {
// 	// Result of conditional expression must be an lvalue so use
// 	// boolean ? lvalue : (throw-expr, lvalue)
// 	return __n < _Nm ? _AT_Type::_S_ref(_M_elems, __n)
// 	  : (std::__throw_out_of_range_fmt(__N("array::at: __n (which is %zu) "
// 					       ">= _Nm (which is %zu)"),
// 					   __n, _Nm),
// 	     _AT_Type::_S_ref(_M_elems, 0));
//       }

//       _GLIBCXX17_CONSTEXPR reference
//       front() noexcept
//       { return *begin(); }

//       constexpr const_reference
//       front() const noexcept
//       { return _AT_Type::_S_ref(_M_elems, 0); }

//       _GLIBCXX17_CONSTEXPR reference
//       back() noexcept
//       { return _Nm ? *(end() - 1) : *end(); }

//       constexpr const_reference
//       back() const noexcept
//       {
// 	return _Nm ? _AT_Type::_S_ref(_M_elems, _Nm - 1)
//  	           : _AT_Type::_S_ref(_M_elems, 0);
//       }

//       _GLIBCXX17_CONSTEXPR pointer
//       data() noexcept
//       { return _AT_Type::_S_ptr(_M_elems); }

//       _GLIBCXX17_CONSTEXPR const_pointer
//       data() const noexcept
//       { return _AT_Type::_S_ptr(_M_elems); }
//     };

// #if __cpp_deduction_guides >= 201606
//   template<typename _Tp, typename... _Up>
//     array(_Tp, _Up...)
//       -> array<enable_if_t<(is_same_v<_Tp, _Up> && ...), _Tp>,
// 	       1 + sizeof...(_Up)>;
// #endif

//   // Array comparisons.
//   template<typename _Tp, std::size_t _Nm>
//     inline bool
//     operator==(const array<_Tp, _Nm>& __one, const array<_Tp, _Nm>& __two)
//     { return std::equal(__one.begin(), __one.end(), __two.begin()); }

//   template<typename _Tp, std::size_t _Nm>
//     inline bool
//     operator!=(const array<_Tp, _Nm>& __one, const array<_Tp, _Nm>& __two)
//     { return !(__one == __two); }

//   template<typename _Tp, std::size_t _Nm>
//     inline bool
//     operator<(const array<_Tp, _Nm>& __a, const array<_Tp, _Nm>& __b)
//     {
//       return std::lexicographical_compare(__a.begin(), __a.end(),
// 					  __b.begin(), __b.end());
//     }

//   template<typename _Tp, std::size_t _Nm>
//     inline bool
//     operator>(const array<_Tp, _Nm>& __one, const array<_Tp, _Nm>& __two)
//     { return __two < __one; }

//   template<typename _Tp, std::size_t _Nm>
//     inline bool
//     operator<=(const array<_Tp, _Nm>& __one, const array<_Tp, _Nm>& __two)
//     { return !(__one > __two); }

//   template<typename _Tp, std::size_t _Nm>
//     inline bool
//     operator>=(const array<_Tp, _Nm>& __one, const array<_Tp, _Nm>& __two)
//     { return !(__one < __two); }

//   // Specialized algorithms.
//   template<typename _Tp, std::size_t _Nm>
//     inline
// #if __cplusplus > 201402L || !defined(__STRICT_ANSI__) // c++1z or gnu++11
//     // Constrained free swap overload, see p0185r1
//     typename std::enable_if<
//       _GLIBCXX_STD_C::__array_traits<_Tp, _Nm>::_Is_swappable::value
//     >::type
// #else
//     void
// #endif
//     swap(array<_Tp, _Nm>& __one, array<_Tp, _Nm>& __two)
//     noexcept(noexcept(__one.swap(__two)))
//     { __one.swap(__two); }

// #if __cplusplus > 201402L || !defined(__STRICT_ANSI__) // c++1z or gnu++11
//   template<typename _Tp, std::size_t _Nm>
//     typename std::enable_if<
//       !_GLIBCXX_STD_C::__array_traits<_Tp, _Nm>::_Is_swappable::value>::type
//     swap(array<_Tp, _Nm>&, array<_Tp, _Nm>&) = delete;
// #endif

//   template<std::size_t _Int, typename _Tp, std::size_t _Nm>
//     constexpr _Tp&
//     get(array<_Tp, _Nm>& __arr) noexcept
//     {
//       static_assert(_Int < _Nm, "array index is within bounds");
//       return _GLIBCXX_STD_C::__array_traits<_Tp, _Nm>::
// 	_S_ref(__arr._M_elems, _Int);
//     }

//   template<std::size_t _Int, typename _Tp, std::size_t _Nm>
//     constexpr _Tp&&
//     get(array<_Tp, _Nm>&& __arr) noexcept
//     {
//       static_assert(_Int < _Nm, "array index is within bounds");
//       return std::move(_GLIBCXX_STD_C::get<_Int>(__arr));
//     }

//   template<std::size_t _Int, typename _Tp, std::size_t _Nm>
//     constexpr const _Tp&
//     get(const array<_Tp, _Nm>& __arr) noexcept
//     {
//       static_assert(_Int < _Nm, "array index is within bounds");
//       return _GLIBCXX_STD_C::__array_traits<_Tp, _Nm>::
// 	_S_ref(__arr._M_elems, _Int);
//     }

//   template<std::size_t _Int, typename _Tp, std::size_t _Nm>
//     constexpr const _Tp&&
//     get(const array<_Tp, _Nm>&& __arr) noexcept
//     {
//       static_assert(_Int < _Nm, "array index is within bounds");
//       return std::move(_GLIBCXX_STD_C::get<_Int>(__arr));
//     }

// } // namespace klib


#endif