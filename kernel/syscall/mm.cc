#include "common.h"
#include "vm.hh"
#include "vm/vmo.hh"
#include "kernel.hh"
#include <asm/mman.h>
#include <asm/errno.h>
#include <linux/mman.h>

namespace syscall
{
    using namespace vm;
    using sys::statcode;
    sysrt_t mmap(addr_t addr,size_t len,int prot,int flags,int fd,int offset){
        auto &curproc=*kHartObj().curtask->getProcess();
        using namespace vm;
        // determine target
        const auto align=[](addr_t addr){return addr2pn(addr);};
        const auto choose=[&](){
            auto rt=curproc.heapTop=ceil(curproc.heapTop);
            curproc.heapTop+=len;
            curproc.heapTop=ceil(curproc.heapTop);
            return addr2pn(rt);
        };
        PageNum vpn,pages;
        if(addr)vpn=align(addr);
        else vpn=choose();
        pages = bytes2pages(len);
        if(!pages)return -1;
        // assert(pages);
        Arc<vm::VMO> vmo;
        /// @todo register for shared mapping
        /// @todo copy content to vmo
        if(fd!=-1){
            auto file=curproc.ofile(fd);
            vmo=file->vmo();
        } else {
            auto pager=make_shared<SwapPager>(nullptr,Segment{0x0,pn2addr(pages)});
            vmo=make_shared<VMOPaged>(pages,pager);
        }
        // actual map
        /// @todo fix flags
        auto mappingType= fd==-1 ?PageMapping::MappingType::file : PageMapping::MappingType::anon;
        auto sharingType=(PageMapping::SharingType)(flags>>8);
        curproc.vmar.map(PageMapping{vpn,pages,0,vmo,PageMapping::prot2perm((PageMapping::Prot)prot),mappingType,sharingType});
        // return val
        return pn2addr(vpn);
    }
    sysrt_t munmap(addr_t addr,size_t len){
        auto &ctx=kHartObj().curtask->ctx;
        auto &curproc=*kHartObj().curtask->getProcess();
        /// @todo len, partial unmap?
        using namespace vm;
        if(addr&vaddrOffsetMask){
            Log(warning,"munmap addr not aligned!");
            return -EINVAL;
        }
        auto region=vm::Segment{addr2pn(addr),addr2pn(addr+len-1)};
        curproc.vmar.unmap(region);
    }
    sysrt_t mprotect(addr_t addr,size_t len,int prot){
        auto curproc=kHartObj().curtask->getProcess();
        // check region validity
        if(addr&vaddrOffsetMask){
            Log(warning,"munmap addr not aligned!");
            return -EINVAL;
        }
        curproc->vmar.protect({addr2pn(addr),addr2pn(addr+len)},PageMapping::prot2perm((PageMapping::Prot)prot));
        return 0;
    }
    sysrt_t madvise(addr_t addr,size_t length,int advice){
        Log(warning,"madvise unimplemented! just ignore it");
        return 0;
    }
    sysrt_t mremap(addr_t oldaddr, size_t oldsize,size_t newsize, int flags, addr_t newaddr){
        auto curproc=kHartObj().curtask->getProcess();
        // check region
        if(oldaddr&vaddrOffsetMask){
            return -EINVAL;
        }
        auto oldpages=bytes2pages(oldsize),newpages=bytes2pages(newsize);
        // St.stub!
        if(newpages>oldpages)
            return -ENOMEM;
        // try remap in place
        auto oldvpn=addr2pn(oldaddr);
        auto shrink=Segment{oldvpn+newpages,oldvpn+oldpages-1};
        curproc->vmar.unmap(shrink);
        return oldaddr;
    }
    sysrt_t mlock(addr_t addr, size_t len){
        // swap is unsupported currently
        return 0;
    }
    sysrt_t munlock(addr_t addr, size_t len){
        // swap is unsupported currently
        return 0;
    }
} // namespace syscall