#ifndef ALLOC_HH__
#define ALLOC_HH__

#include "common.h"
#include "klib.h"
#include "vm.hh"
#include "lock.hh"
#include <EASTL/slist.h>
// #define DEBUG 1
namespace alloc
{
    using vm::PageNum;
    typedef klib::pair<PageNum,int> Span;
    class PageMgr{
    public:
        PageMgr(xlen_t start,xlen_t end);
        ~PageMgr();
        xlen_t alloc(size_t pages);
        void free(PageNum ppn,int order);
        inline void print(){
            Log(debug,"buddy: |"); for(xlen_t i=0;i<buddyTreeSize;i++)Log(debug,"%d | ",buddyNodes[i]-1);Log(debug,"\n");
        }
        void freeUnaligned(PageNum ppn,PageNum pages);
    private:
        inline constexpr xlen_t lsub(xlen_t x){return ((x+1)<<1)-1;}
        inline constexpr xlen_t rsub(xlen_t x){return ((x+1)<<1);}
        inline constexpr xlen_t prnt(xlen_t x){return ((x+1)>>1)-1;}
        inline xlen_t pos2node(xlen_t pos,int order){
            DBG(assert(((pos>>rootOrder)&1)==0);)
            xlen_t nd=0;
            for(int size=rootOrder-1;size>=order;size-=1){
                if((pos>>size)&1)nd=rsub(nd);
                else nd=lsub(nd);
            }
            return nd;
        }

        PageNum start;
        PageNum end;
        xlen_t rootOrder; // not biased
        xlen_t buddyTreeSize;
        uint8_t *buddyNodes;
        // constexpr static int maxOrder=32;
        // klib::list<Span> freelist[maxOrder]; // listnode should be stored in each span's head
    };
    inline void printSpan(const alloc::Span &d){Log(debug,"0x%lx[%lx] ",d.first,d.second);}

    class HeapMgr{
    protected:
        ptr_t pool;
        constexpr static int retryLimit=2;
        virtual void growHeap(size_t size){}
    public:
        HeapMgr(ptr_t addr,xlen_t len);
        virtual ~HeapMgr();
        ptr_t alloc(xlen_t size);
        ptr_t alligned_alloc(xlen_t size,xlen_t alignment);
        void free(ptr_t ptr);
        ptr_t realloc();
    };
    using eastl::slist;
    using eastl::SListNode;
    class HeapMgrGrowable:public HeapMgr{
        class Allocator{
        public:
            explicit Allocator(const char* pName){}
            void* allocate(size_t n, int flags = 0);
            void* allocate(size_t n, size_t alignment, size_t offset, int flags = 0);
            void  deallocate(void* p, size_t n);
        };
        LockedObject<PageMgr>& pmgr;
        slist<Span,Allocator> dynPages;
        void growHeap(size_t size) override;
    public:
        HeapMgrGrowable(HeapMgr &other,LockedObject<PageMgr> &pmgr);
        HeapMgrGrowable(ptr_t addr,xlen_t len,LockedObject<PageMgr> &pmgr);
        ~HeapMgrGrowable();
    };
} // namespace alloc

#endif