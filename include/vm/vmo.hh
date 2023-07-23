#include "vm.hh"
#include "vm/pager.hh"
#include "vm/pcache.hh"
#include "kernel.hh"

namespace vm
{
    class VMOPaged:public VMO{
        vector<PBufRef> pages;
    public:
        Arc<Pager> pager;
        VMOPaged(PageNum len,Arc<Pager> pager):pages(len),pager(pager){}
        VMOPaged(const VMOPaged& other):pages(other.len()),pager(other.pager){}
        inline PageNum len() const override{return pages.size();}
        inline PageSlice req(PageNum offset) override{
            assert(offset<pages.size());
            if(!pages[offset]){
                // load page
                pages[offset]=pager->load(offset);
            }
            assert(pages[offset].use_count());
            return {offset,pages[offset]->ppn,1};
        }
        inline vector<tuple<PageNum,PageNum>> req(const Segment& region){}
        virtual string toString() const{return klib::format("<VMOPaged>{len=0x%x}",len());}
        // inline Arc<VMO> shallow(PageNum start,PageNum end) override{
        //     auto rt=make_shared<VMOPaged>(*this);
        //     rt->pages.assign(pages.begin()+start,pages.begin()+end+1);
        //     return rt;
        // }
        inline Arc<VMO> clone() const override{
            auto rt=make_shared<VMOPaged>(*this);
            rt->pager=make_shared<SwapPager>(pager);
            return rt;
        }
    };

    class VMOContiguous:public VMO{
        const PageNum ppn_, pages_;
    public:
        explicit VMOContiguous(PageNum ppn, PageNum pages) : ppn_(ppn), pages_(pages){}
        ~VMOContiguous() { kGlobObjs->pageMgr->freeUnaligned(ppn_,pages_); }
        inline PageNum len() const override{ return pages_; }
        inline PageNum ppn() const{ return ppn_; }
        inline string toString() const override{ return klib::format("<VMOCont>{%x[%x]}", ppn(), len()); }

        inline PageSlice req(PageNum offset) override{
            return {0,ppn_,pages_};
        }
        bool operator==(const VMOContiguous &other) { return ppn_ == other.ppn_ && pages_ == other.pages_; }
        // inline Arc<VMO> shallow(PageNum start,PageNum end) override{panic("can't shallow copy contiguous vmo");}
        inline Arc<VMO> clone() const override{
            /// @bug alloc alligned but free unaligend
            auto rt=make_shared<VMOContiguous>(kGlobObjs->pageMgr->alloc(pages_),pages_);
            copyframes(ppn_,rt->ppn(),rt->len());
            return rt;
        }
    };

} // namespace vm
