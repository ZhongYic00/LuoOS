#include "vm.hh"
#include "bio.hh"
#include <EASTL/vector.h>

namespace vm
{
    class VnodePager:public Pager{
        Arc<fs::INode> vnode;
    public:
        VnodePager(Arc<fs::INode> vnode):vnode(vnode){}
        PBufRef load(PageNum offset) override{
            // vnode->nodRead();
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
