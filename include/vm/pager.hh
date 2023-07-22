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
        PageNum backingoff;
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
            if(!pages.count(offset)){
                auto pbuf=pages[offset]=make_shared<PageBuf>(PageKey{.swp={this,(uint32_t)offset}});
                if(backing && backing->contains(backingoff+offset)){
                    // fill from backing
                    backing->loadTo(backingoff+offset,pbuf);
                } else {
                    // zero filled
                    pbuf->fillzero();
                }
            }
            return pages[offset];
        }
        void put(PBufRef pbuf) override{
        }
        bool contains(PageNum offset){return true;}
    };
} // namespace vm
