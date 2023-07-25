#pragma once

#include "vm.hh"
#include "bio.hh"

namespace vm
{
    struct SwapKey{
        ptr_t pager;
        PageNum offset;
        bool operator==(const SwapKey &other) const {return pager==other.pager && offset==other.offset;}
    };
    struct PageKey{
        bio::BlockKey dev;
        SwapKey swp;
        bool isDevice() const{return true;}
        bool operator==(const PageKey &other) const {return other.swp==swp;}
    };
} // namespace vm

namespace eastl{
    template <> struct hash<vm::PageKey>
    { size_t operator()(const vm::PageKey &val) const { return size_t(val.swp.pager); } };
    template <> struct hash<vm::SwapKey>
    { size_t operator()(vm::SwapKey val) const { return size_t(val.pager); } };
}

namespace vm{
    struct PageBuf{
        const PageKey key;
        const PageNum ppn;
        PageBuf(const PageKey key_);
        ~PageBuf();
        inline void fillzero(){memset((ptr_t)pn2addr(ppn),0,pageSize);}
        inline void fillwith(PageBuf& other){memmove((ptr_t)pn2addr(ppn),(ptr_t)pn2addr(other.ppn),vm::pageSize);}
    };
    typedef Arc<PageBuf> PBufRef;

    class PageCacheMgr{
    eastl::shared_lru<PageKey,PageBuf> lru;
    public:
        constexpr static size_t defaultSize=0x100;
        PageCacheMgr():lru(defaultSize){}
        PBufRef operator[](const PageKey &key){
            Log(debug,"pcache[{%d:%x}]",key.dev.dev,key.dev.secno);
            return lru.getOrSet(key,[key]()->PageBuf*{return new PageBuf(key);});
        }
    };

}