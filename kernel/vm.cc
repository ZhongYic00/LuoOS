#include "vm.hh"
#include "alloc.hh"
#include "kernel.hh"
#include "rvcsr.hh"

// #define moduleLevel LogLevel::debug

using namespace vm;
PageBuf::PageBuf(const PageKey key_):key(key),ppn(kGlobObjs->pageMgr->alloc(1)){
    Log(info,"alloc(%x)",ppn);
}
PageBuf::~PageBuf(){
    Log(info,"free(%x)",ppn);
    kGlobObjs->pageMgr->free(ppn,0);
}

VMOMapper::VMOMapper(Arc<VMO> vmo){
    auto mapping=PageMapping{addr2pn(kInfo.segments.frames.second)+5,vmo->len(),0,vmo,PageTableEntry::fieldMasks::r|PageTableEntry::fieldMasks::w|PageTableEntry::fieldMasks::v2,PageMapping::MappingType::file,PageMapping::SharingType::shared};
    kGlobObjs->vmar->map(mapping,true);
    region=mapping.vsegment();
}
VMOMapper::~VMOMapper(){kGlobObjs->vmar->unmap(region);}

void VMAR::map(const PageMapping &mapping,bool ondemand){
    /// @todo always forget to set v-bit or u-bit
    Log(debug, "map %s", mapping.toString().c_str());
    mappings.insert(mapping);
    /// @todo on-demand paging
    if(!ondemand)
    for(PageNum i=0;i<mapping.pages();){
        auto [off,ppn,pages]=mapping.req(i);
        auto voff=off-mapping.offset;
        pagetable.createMapping(mapping.vpn+voff,ppn,pages,mapping.perm);
        /// @bug |vmo.start...i...start+pages|
        i=voff+pages;
    }
    Log(debug, "after map, VMAR:%s", klib::toString(mappings).c_str());
    Log(debug,"pagetable: %s",pagetable.toString().c_str());
}
void VMAR::protect(const Segment region, perm_t perm){
    Log(debug, "protect [%x,%x]=>%x", region.l,region.r,perm);
    // bf
    auto l = mappings.begin();
    auto r = mappings.end();
    for (auto i = l; i != r;){
        if (auto ovlp=i->vsegment() & region){
            pagetable.removeMapping(*i);
            // left part
            if(auto lregion=Segment{i->vpn,ovlp.l-1}){
                auto lslice=i->splitChild(lregion);
                map(lslice);
            }
            // right part
            if(auto rregion=Segment{ovlp.r+1,i->vend()}){
                auto rslice=i->splitChild(rregion);
                map(rslice);
            }
            auto newmapping=i->splitChild(ovlp,perm);
            /// @bug order may be incorrect
            // remove origin
            i=mappings.erase(i);
            map(newmapping);
        } else i++;
    }
    Log(debug, "after protect, VMAR:%s", klib::toString(mappings).c_str());
}
void VMAR::reset()
{
    Log(debug,"before reset, VMAR:%s",klib::toString(mappings).c_str());
    for(auto it=mappings.begin();it!=mappings.end();){
        if(it->mapping!=PageMapping::MappingType::system){
            Log(debug,"unmap %s",it->toString().c_str());
            pagetable.removeMapping(*it);
            it=mappings.erase(it);
        } else it++;
    }
    Log(debug,"after reset, VMAR:%s",klib::toString(mappings).c_str());
    Log(debug,"pagetable: %s",pagetable.toString().c_str());
}
void VMAR::copyout(xlen_t addr,const ByteArray &buff) {
    // @todo 检查拷贝后是否会越界（addr+buff.len后超出用户进程大小）
    for(xlen_t va_begin = addr, src_begin = (xlen_t)buff.buff, len = buff.len, cpylen = 0; len > 0; va_begin += cpylen, src_begin += cpylen, len -= cpylen) {
        auto mapping = find(va_begin);
        VMOMapper mapper(mapping->vmo);
        xlen_t off = va_begin - pn2addr(mapping->vpn) + pn2addr(mapping->offset);
        xlen_t pa_begin = mapper.start() + off;
        xlen_t pa_end = pa_begin + len - 1;  // 包括pa_end
        xlen_t pa_bound = mapper.end();  // 包括pa_bound
        pa_end = pa_end>pa_bound ? pa_bound : pa_end;  // 包括pa_end
        cpylen = pa_end - pa_begin + 1;
        memmove((void*)pa_begin, (void*)src_begin, cpylen);
    }
}