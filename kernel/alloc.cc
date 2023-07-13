#include "alloc.hh"
#include "thirdparty/tlsf.h"
#include "kernel.hh"


using namespace alloc;

#define moduleLevel warning

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
    Log(warning,"growHeap(growsize=%d)\n",growsize);
    Span newSpan={pmgr->alloc(growsize),growsize};
    dynPages.front()=newSpan;
    ptr_t newPool=(ptr_t)vm::pn2addr(newSpan.first);
    tlsf_add_pool(newPool,growsize*vm::pageSize);
    dynPages.push_front({0,0});
    DBG(
        Log(debug,"newPool=%p\n",newPool);
        tlsf_walk_pool(newPool);
    )
}
HeapMgrGrowable::HeapMgrGrowable(ptr_t addr,xlen_t len,LockedObject<PageMgr> &pmgr):HeapMgr(addr,len),pmgr(pmgr),growsize(32){
    dynPages.push_front({0,0});
}
HeapMgrGrowable::HeapMgrGrowable(HeapMgr &other,LockedObject<PageMgr> &pmgr):pmgr(pmgr),HeapMgr(other),growsize(32){
    dynPages.push_front({0,0});
}
HeapMgrGrowable::~HeapMgrGrowable(){
    DBG(Log(debug,"%s()\n",__func__);)
    // DBG(
        Log(debug,"pool=%p\n",pool);
        tlsf_walk_pool(pool);
        if(!dynPages.empty())for(auto span:dynPages){
            tlsf_walk_pool((ptr_t)vm::pn2addr(span.first));
        }
    // )
    while(!dynPages.empty()){
        auto span=dynPages.front();dynPages.pop_front();
        // pmgr.free(span.first,klib::log2up(span.second));
        DBG(Log(debug,"0x%lx %d\n",span.first,span.second);)
    }
}
void* HeapMgrGrowable::Allocator::allocate(size_t n, int flags){return tlsf_malloc(n);}
void* HeapMgrGrowable::Allocator::allocate(size_t n, size_t alignment, size_t offset, int flags){return tlsf_memalign(alignment,n);}
void  HeapMgrGrowable::Allocator::deallocate(void* p, size_t n){return tlsf_free(p);}

ptr_t HeapMgr::alloc(xlen_t size){
    if(size==0)return nullptr;
    for(int retry=0;retry<retryLimit;retry++){
        ptr_t rt=(tlsf_malloc(size));
        if(rt!=nullptr)return rt;
        Log(warning,"cannot alloc %d in current pool,alloc new",size);
        growHeap();
    }
    Log(warning,"alloc failed");
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

PageMgr::PageMgr(PageNum start,PageNum end):start(start),end(end){
    DBG(
        Log(debug,"PageMgr(start=%lx,end=%lx)\n",start,end);
    )
    xlen_t pages=end-start;
/* freelist+pageheap version, not accomplished
    for(int k=0;k<maxOrder && pages;pages>>=1,k++){
        DBG(Log(debug,"k=%d,pages=0x%lx\n",k,pages);)
        if(pages&1){
            auto node=reinterpret_cast<klib::ListNode<Span>*>(vm::pn2addr(start));
            DBG(Log(debug,"node@0x%lx\n",start);)
            node->data=(Span){start,1l<<k};
            freelist[k].push_back(node);
            start+=1l<<k;
        }
    }
    DBG(
    for(int k=0;k<maxOrder;k++){
        Log(debug,"freelist[%d]:\n",k);
        freelist[k].print(printSpan);
    })
    assert(start==end);
*/
    rootOrder=klib::log2up(pages);
    buddyTreeSize=(1l<<rootOrder)*2-1;
    // DBG(Log(debug,"pages=%d order=%d rootOrder=%ld buddyTreeSize=%ld\n",pages,rootOrder,rootOrder,buddyTreeSize);)
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
        // this->print();
        Log(debug,"PageMgr::PageMgr() over\n");
    )
}
PageMgr::~PageMgr(){ delete[] buddyNodes; }

PageNum PageMgr::alloc(size_t pages){
    int rounded=klib::log2up(pages)+1; //biased size
    if(buddyNodes[0]<rounded) return 0;
    xlen_t node=0;
    for(xlen_t nd=0,size=rootOrder+1;rounded<=size;size-=1){ // find suitable node; size is biased size
        Log(trace,"nd=%ld ",nd);
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
    Log(debug,"node=0x%lx ppn=0x%lx\n",node,ppn);
    assert(ppn<end);
    return ppn;
}
PageNum PageMgr::free(PageNum ppn,int order){
    xlen_t idx=pos2node(ppn-start,order);
    DBG(Log(debug,"PageMgr::free(pos=%ld order=%d) idx=0x%lx\n",ppn-start,order,idx);)
    // xlen_t idx=ppn-start+(1l<<rootOrder);
    buddyNodes[idx]=order+1;
    for(idx=prnt(idx);idx>0;idx=prnt(idx)){
        buddyNodes[idx]=buddyNodes[lsub(idx)]==buddyNodes[rsub(idx)]?buddyNodes[lsub(idx)]+1:klib::max(buddyNodes[lsub(idx)],buddyNodes[rsub(idx)]); // the free'd leaf node ensures lsub or rsub being not null
    }
    buddyNodes[0]=klib::max(buddyNodes[lsub(0)],buddyNodes[rsub(0)]);
    // DBG(this->print();)
}

void* operator new(size_t size,ptr_t ptr){ // placement new
    return ptr;
}
void* operator new(size_t size){
    if(size<vm::pageSize*32)
        return kGlobObjs->heapMgr->alloc(size);
    else return (ptr_t)vm::pn2addr(kGlobObjs->pageMgr->alloc(vm::bytes2pages(size)));
}
void* operator new(size_t size,std::align_val_t alignment){
    if(size<vm::pageSize*32)
        return kGlobObjs->heapMgr->alligned_alloc(size,size_t(alignment));
    else panic("unimplemented!");
}
void* operator new[](size_t size){
    if(size<vm::pageSize*32)
        return kGlobObjs->heapMgr->alloc(size);
    else return (ptr_t)vm::pn2addr(kGlobObjs->pageMgr->alloc(vm::bytes2pages(size)));
}
void operator delete(void* ptr){
    kGlobObjs->heapMgr->free(ptr);
}
void operator delete(void* ptr,xlen_t unknown){
    kGlobObjs->heapMgr->free(ptr);
}
void operator delete(void* ptr,xlen_t unknown,std::align_val_t){
    kGlobObjs->heapMgr->free(ptr);
}
void operator delete[](void* ptr){
    kGlobObjs->heapMgr->free(ptr);
}