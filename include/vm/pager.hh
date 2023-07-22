#pragma once
#include "vm.hh"
#include "vm/pcache.hh"
#include "bio.hh"
#include "kernel.hh"
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>

namespace vm
{
    class VnodePager:public Pager{
        Arc<fs::INode> vnode;
        eastl::unordered_map<PageNum,PBufRef> pages;
    public:
        VnodePager(Arc<fs::INode> vnode):vnode(vnode){}
        PBufRef load(PageNum offset) override{
            if(!pages.count(offset))pages[offset]=make_shared<PageBuf>(PageKey{.dev={0,(uint32_t)offset}});
            auto pbuf=pages[offset];
            vnode->readPages(fs::memvec{Segment{offset,offset}},fs::memvec{Segment{pbuf->ppn,pbuf->ppn}});
            return pbuf;
        }
        void put(PBufRef pbuf) override{
        }
    };
    class SwapPager:public Pager{
        Arc<Pager> backing;
        PageNum backingoff;
        bool inSwap(PageNum offset){return false;}
    public:
        SwapPager(Arc<Pager> other){
            if(auto swppager=eastl::dynamic_pointer_cast<SwapPager>(other)){
                backing=swppager->backing;
                backingoff=swppager->backingoff;
            } else {
                backing=other;
                backingoff=0;
            }
        }
        PBufRef load(PageNum offset) override{
            if(inSwap(offset)){}
            auto pbuf=kGlobObjs->pageCache[PageKey{.swp={this,offset}}];
            if(backing){
                // fill from backing
            } else {
                // zero filled
                pbuf->fillzero();
            }
            return pbuf;
        }
        void put(PBufRef pbuf) override{
        }
    };
} // namespace vm
