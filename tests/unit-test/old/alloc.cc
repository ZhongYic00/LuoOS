#include "../include/common.h"
#include "klib.h"
#include "tlsf.h"
#include "vm.hh"
#define DEBUG 1
namespace alloc
{
    using vm::PageNum;
    typedef klib::pair<PageNum,int> Span;
    class PageMgr{
    public:
        PageMgr(xlen_t start,xlen_t end);
        ~PageMgr();
        xlen_t alloc(size_t pages);
        xlen_t free(xlen_t pages,int order);
        inline void print(){
            printf("buddy: |"); for(xlen_t i=0;i<buddyTreeSize;i++)printf("%d | ",buddyNodes[i]-1);printf("\n");
        }
    private:
        // void split();
        // void merge();
        inline constexpr xlen_t lsub(xlen_t x){return ((x+1)<<1)-1;}
        inline constexpr xlen_t rsub(xlen_t x){return ((x+1)<<1);}
        inline constexpr xlen_t prnt(xlen_t x){return ((x+1)>>1)-1;}
        // inline constexpr xlen_t lsib(xlen_t x){return x-1;}
        // inline constexpr xlen_t rsib(xlen_t x){return x+1;}
        inline xlen_t pos2node(xlen_t pos,int order){
            DBG(assert(((pos>>rootOrder)&1)==0);)
            xlen_t nd=0;
            for(int size=rootOrder-1;size>=order;size-=1){
                if((pos>>size)&1)nd=rsub(nd);
                else nd=lsub(nd);
                // DBG(printf("%ld ",nd);)
            }
            // DBG(printf("\n");)
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
    inline void printSpan(const alloc::Span &d){printf("0x%lx[%lx] ",d.first,d.second);}

    class HeapMgr{
    protected:
        ptr_t pool;
        constexpr static int retryLimit=2;
        inline virtual void growHeap(){}
    public:
        HeapMgr(ptr_t addr,xlen_t len);
        virtual ~HeapMgr();
        ptr_t alloc(xlen_t size);
        ptr_t alligned_alloc(xlen_t size,xlen_t alignment);
        void free(ptr_t ptr);
        ptr_t realloc();
    };
    class HeapMgrGrowable:public HeapMgr{
        PageMgr& pmgr;
        klib::list<Span> dynPages;
        klib::ListNode<Span> *reservedNode;
        int growsize;
        void growHeap() override;
    public:
        HeapMgrGrowable(HeapMgr &other,PageMgr &pmgr);
        HeapMgrGrowable(ptr_t addr,xlen_t len,PageMgr &pmgr);
        ~HeapMgrGrowable();
    };
} // namespace alloc
    


using namespace alloc;

HeapMgr::HeapMgr(ptr_t addr,xlen_t len){
    /*
    tlsf_create() create ctrl, pool can be added via tlsf_add_pool()
    tlsf_create_with_pool() create and add a pool
    */
    this->pool=addr;
    tlsf_create_with_pool(reinterpret_cast<ptr_t>(addr),len);
}
HeapMgr::~HeapMgr(){}

void HeapMgrGrowable::growHeap(){
    DBG(
        printf("%s(growsize=%d)\n",__func__,growsize);
    )
    Span newSpan={pmgr.alloc(growsize),growsize};
    reservedNode->data=newSpan;
    ptr_t newPool=(ptr_t)vm::pn2addr(newSpan.first);
    tlsf_add_pool(newPool,growsize*vm::pageSize);
    dynPages.push_back(reservedNode);
    reservedNode=(klib::ListNode<Span>*)(alloc(sizeof(klib::ListNode<Span>)));
    DBG(
        printf("newPool=%p reservedNode=%p\n",newPool,reservedNode);
        tlsf_walk_pool(newPool);
    )
}
HeapMgrGrowable::HeapMgrGrowable(ptr_t addr,xlen_t len,PageMgr &pmgr):HeapMgr(addr,len),pmgr(pmgr),growsize(2){
    reservedNode=(klib::ListNode<Span>*)(alloc(sizeof(klib::ListNode<Span>)));
}
HeapMgrGrowable::HeapMgrGrowable(HeapMgr &other,PageMgr &pmgr):pmgr(pmgr),HeapMgr(other),growsize(2){
    reservedNode=(klib::ListNode<Span>*)(alloc(sizeof(klib::ListNode<Span>)));
}
HeapMgrGrowable::~HeapMgrGrowable(){
    DBG(printf("%s()\n",__func__);)
    // DBG(
        printf("pool=%p\n",pool);
        tlsf_walk_pool(pool);
        if(!dynPages.empty())for(auto node=dynPages.head;node->iter.next!=nullptr;node=node->iter.next){
            tlsf_walk_pool((ptr_t)vm::pn2addr(node->data.first));
        }
    // )
    while(!dynPages.empty()){
        auto span=dynPages.pop_front();
        // pmgr.free(span.first,klib::log2up(span.second));
        DBG(printf("0x%lx %d\n",span.first,span.second);)
    }
}

ptr_t HeapMgr::alloc(xlen_t size){
    for(int retry=0;retry<retryLimit;retry++){
        ptr_t rt=(tlsf_malloc(size));
        if(rt!=nullptr)return rt;
        growHeap();
    }
    return nullptr;
}

ptr_t HeapMgr::alligned_alloc(xlen_t size,xlen_t alignment){
    for(int retry=0;retry<retryLimit;retry++){
        ptr_t rt=(tlsf_memalign(alignment,size));
        if(rt!=nullptr)return rt;
        growHeap();
    }
    return nullptr;
}
void HeapMgr::free(ptr_t ptr){ tlsf_free(ptr); }


uint8_t pool[0x2000];
alignas(0x1000) uint8_t pages[0xf7000];
HeapMgr initHeapMgr(&pool,2*vm::pageSize);
HeapMgr *kHeapMgr=&initHeapMgr;

void* operator new(size_t size){
    return kHeapMgr->alloc(size);
}
void operator delete(void* ptr){
    return kHeapMgr->free(ptr);
}

PageMgr::PageMgr(PageNum start,PageNum end):start(start),end(end){
    DBG(
        printf("PageMgr(start=%lx,end=%lx)\n",start,end);
    )
    xlen_t pages=end-start;
/* freelist+pageheap version, not accomplished
    for(int k=0;k<maxOrder && pages;pages>>=1,k++){
        DBG(printf("k=%d,pages=0x%lx\n",k,pages);)
        if(pages&1){
            auto node=reinterpret_cast<klib::ListNode<Span>*>(vm::pn2addr(start));
            DBG(printf("node@0x%lx\n",start);)
            node->data=(Span){start,1l<<k};
            freelist[k].push_back(node);
            start+=1l<<k;
        }
    }
    DBG(
    for(int k=0;k<maxOrder;k++){
        printf("freelist[%d]:\n",k);
        freelist[k].print(printSpan);
    })
    assert(start==end);
*/
    rootOrder=klib::log2up(pages);
    buddyTreeSize=(1l<<rootOrder)*2-1;
    DBG(printf("pages=%d order=%d rootOrder=%ld buddyTreeSize=%ld\n",pages,rootOrder,rootOrder,buddyTreeSize);)
    buddyNodes=new uint8_t[buddyTreeSize];
    for(xlen_t i=1;i<buddyTreeSize;i++)buddyNodes[i]=0;
    {xlen_t base=start;
    for(int i=rootOrder;i>=0;i--){
        if((pages>>i)&1){
            free(base,i);
            base+=(1l<<i);
        }
    }}
    DBG(
        this->print();
        printf("PageMgr::PageMgr() over\n");
    )
}
PageMgr::~PageMgr(){ delete[] buddyNodes; }

PageNum PageMgr::alloc(size_t pages){
    int rounded=klib::log2up(pages)+1; //biased size
    if(buddyNodes[0]<rounded) return 0;
    xlen_t node=0;
    for(xlen_t nd=0,size=rootOrder+1;rounded<=size;size-=1){ // find suitable node; size is biased size
        DBG(printf("nd=%ld ",nd);)
        if(buddyNodes[nd]==size && buddyNodes[nd]==rounded) node=nd;
        else {
            if(buddyNodes[nd]==size){ // split to smaller
                buddyNodes[lsub(nd)]=buddyNodes[rsub(nd)]=size-1;
                nd=lsub(nd);
            } else {
                if(klib::min(buddyNodes[lsub(nd)],buddyNodes[rsub(nd)])>=rounded)
                    nd=buddyNodes[lsub(nd)]<buddyNodes[rsub(nd)]?lsub(nd):rsub(nd);
                else
                    nd=buddyNodes[lsub(nd)]>buddyNodes[rsub(nd)]?lsub(nd):rsub(nd);
            }
        }
    }
    assert(node>0);
    buddyNodes[node]=0;
    for(xlen_t nd=prnt(node);nd>0;nd=prnt(nd))
        buddyNodes[nd]=klib::max(buddyNodes[lsub(nd)],buddyNodes[rsub(nd)]);
    buddyNodes[0]=klib::max(buddyNodes[lsub(0)],buddyNodes[rsub(0)]);
    
    PageNum ppn=start+(node+1-(1l<<(rootOrder+1-rounded)))*(1l<<(rounded-1));//((node+1)*rounded-(1l<<rootOrder));
    DBG(
        printf("node=0x%lx ppn=0x%lx\n",node,ppn);
    )
    assert(ppn<end);
    return ppn;
}
PageNum PageMgr::free(PageNum ppn,int order){
    xlen_t idx=pos2node(ppn-start,order);
    DBG(printf("PageMgr::free(pos=%ld order=%d) idx=0x%lx\n",ppn-start,order,idx);)
    // xlen_t idx=ppn-start+(1l<<rootOrder);
    buddyNodes[idx]=order+1;
    for(idx=prnt(idx);idx>0;idx=prnt(idx)){
        buddyNodes[idx]=buddyNodes[lsub(idx)]==buddyNodes[rsub(idx)]?buddyNodes[lsub(idx)]+1:klib::max(buddyNodes[lsub(idx)],buddyNodes[rsub(idx)]); // the free'd leaf node ensures lsub or rsub being not null
    }
    buddyNodes[0]=klib::max(buddyNodes[lsub(0)],buddyNodes[rsub(0)]);
    // DBG(this->print();)
}

int main(){

    printf("pages: %lx %lx\n",&pages,&pages+sizeof(pages));
    PageMgr pmgr(vm::addr2pn((xlen_t)(&pages))+2,vm::addr2pn((xlen_t)(&pages))+0xf7);

    kHeapMgr=new HeapMgrGrowable(*kHeapMgr,pmgr);

    int reqs[]={1,1,2,4,1};
    int res[sizeof(reqs)];
    for(int i=0;i<sizeof(reqs)/sizeof(int);i++){
        res[i]=pmgr.alloc(reqs[i]);
        printf("alloc %ldpages, res=0x%lx\n",reqs[i],res[i]);
        // pmgr.print();
    }
    for(int i=sizeof(reqs)/sizeof(int)-1;i>=0;i--){
        pmgr.free(res[i],klib::log2up(reqs[i]));
    }
    
    int areqs[]={sizeof(int),1000,0x1000,0x1000,0x1000,0x1320};
    int atypes[]={0,0,1,1,1,0};
    ptr_t ares[sizeof(areqs)/sizeof(int)];
    for(int i=0;i<sizeof(areqs)/sizeof(int);i++){
        if(atypes[i]) ares[i]=kHeapMgr->alligned_alloc(areqs[i],areqs[i]);
        else ares[i]=kHeapMgr->alloc(areqs[i]);
        printf("alloc(%d) res=%p\n",areqs[i],ares[i]);
    }
    for(int i=0;i<sizeof(areqs)/sizeof(int);i++){
        if(ares[i])kHeapMgr->free(ares[i]);
    }
    // test reservedNode
    // for(int i=0;i<8000;i++){
    //     printf("alloc res=%p\n",kHeapMgr->alloc(4));
    // }
    tlsf_walk_pool(0);
    return 0;
}