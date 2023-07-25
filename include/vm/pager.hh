#pragma once
#include "vm.hh"
#include "vm/pcache.hh"
#include "bio.hh"
#include "kernel.hh"

namespace vm
{
    class VnodePager:public Pager{
        Arc<fs::INode> vnode;
        eastl::unordered_map<PageNum,PBufRef> pages;
    public:
        VnodePager(Arc<fs::INode> vnode):vnode(vnode){}
        void loadTo(PageNum offset,PBufRef pbuf) override{
            vnode->readPages(fs::memvec{Segment{offset,offset}},fs::memvec{Segment{pbuf->ppn,pbuf->ppn}});
        }
        PBufRef load(PageNum offset) override{
            if(!pages.count(offset)){
                auto pbuf=pages[offset]=make_shared<PageBuf>(PageKey{.dev={0,(uint32_t)offset}});
                loadTo(offset,pbuf);
            }
            return pages[offset];
        }
        void put(PBufRef pbuf) override{
        }
        bool contains(PageNum offset) override{return offset>=0&&offset<bytes2pages(vnode->rFileSize());}
    };
    class SwapPager:public Pager{
        Arc<Pager> backing;
        eastl::unordered_map<PageNum,PBufRef> pages;
        bool inSwap(PageNum offset){return false;}
    public:
        Segment backingregion;
        SwapPager(Arc<Pager> other){
            if(auto swppager=eastl::dynamic_pointer_cast<SwapPager>(other)){
                if(swppager->backing)backing=swppager->backing;
                else backing=swppager;
                backingregion=swppager->backingregion;
            } else {
                backing=other;
            }
        }
        PBufRef load(PageNum offset) override{
            if(inSwap(offset)){}
            if(!pages.count(offset)){
                auto pbuf=make_shared<PageBuf>(PageKey{.swp={this,(uint32_t)offset}});
                loadTo(offset,pbuf);
                pages[offset]=pbuf;
            }
            return pages[offset];
        }
        void loadTo(PageNum offset,PBufRef pbuf) override{
            if(!pages.count(offset)){
                auto backingoff=addr2pn(backingregion.l)+offset;
                if(backing && backingoff<=addr2pn(backingregion.r)){
                    // fill from backing
                    backing->loadTo(backingoff,pbuf);
                    if(offset==0){
                        auto partial=backingregion.l-pn2addr(backingoff);
                        memset((ptr_t)pn2addr(pbuf->ppn),0,partial);
                    }
                    if(backingoff==addr2pn(backingregion.r)){
                        auto partial=backingregion.r-pn2addr(backingoff);
                        memset((ptr_t)pn2addr(pbuf->ppn)+partial,0,pageSize-partial);
                    }
                } else {
                    // zero filled
                    pbuf->fillzero();
                }
            } else {
                pbuf->fillwith(*pages[offset]);
            }
        }
        void put(PBufRef pbuf) override{
        }
        bool contains(PageNum offset){return true;}
    };
} // namespace vm
