#include "alloc.hh"
#include "thirdparty/tlsf.h"


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
// HeapMgr initHeapMgr(&pool,2*vm::pageSize);
HeapMgr *kHeapMgr=nullptr;

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
