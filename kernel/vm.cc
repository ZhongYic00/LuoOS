#include "vm.hh"
#include "alloc.hh"
#include "kernel.hh"
#define DEBUG 1

using namespace vm;
void PageTable::createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm,int level){
    xlen_t bigPageSize=1l<<(9*level);
    xlen_t unaligned=vpn&(bigPageSize-1);
    DBG(
        printf("createMapping(table,vpn=0x%lx,ppn=0x%lx,pages=0x%lx,level=%d)\n",vpn,ppn,pages,level);
        printf("bigPageSize=%lx,unaligned=%lx\n",bigPageSize,unaligned);
    )
    // align vpn to boundary
    if(unaligned){
        auto partial=klib::min(bigPageSize-unaligned,pages);
        PageTableEntry &entry=table[(vpn/bigPageSize)&vpnMask];
        pgtbl_t subTable=nullptr;
        if(entry.isValid() && !entry.isLeaf()){ // if the range is previously managed, and is not bigPage
            subTable=entry.child();
        }
        else{
            subTable=createPTNode();
            if(entry.isValid() && entry.isLeaf()){ // if is bigPage, change to ptnode and pushdown
                xlen_t prevppn=entry.ppn();
                perm_t prevPerm=entry.raw.perm;
                entry.setPTNode();
                createMapping(subTable,vpn-unaligned,prevppn,unaligned,prevPerm,level-1); // pushdown
            }
            entry.setValid();entry.raw.ppn=addr2pn(reinterpret_cast<xlen_t>(subTable));
        }
        DBG(entry.print();)
        DBG(printf("subtable=%lx\n",subTable);)
        createMapping(subTable,vpn,ppn,partial,perm,level-1); // actual create mapping
        vpn+=partial,ppn+=partial,pages-=partial;
    }
    // map aligned whole pages
    for(int i=(vpn/bigPageSize)&vpnMask;pages>=bigPageSize;i++){
        // create big page entry
        PageTableEntry &entry=table[i];
        if(entry.isValid()){} // remapping a existing vaddr
        entry.setValid();
        entry.raw.perm=perm;
        entry.raw.ppn=ppn;
        DBG(entry.print();)

        vpn+=bigPageSize;
        ppn+=bigPageSize;
        pages-=bigPageSize;
    }
    // map rest pages
    if(pages){
        assert(pages>0);
        PageTableEntry &entry=table[(vpn/bigPageSize)&vpnMask];
        pgtbl_t subTable=nullptr;
        if(entry.isValid() && !entry.isLeaf()){ // if the range is previously managed, and is not bigPage
            subTable=entry.child();
        }
        else{
            subTable=createPTNode();
            if(entry.isValid() && entry.isLeaf()){
                xlen_t prevppn=entry.ppn();
                perm_t prevPerm=entry.raw.perm;
                entry.setPTNode();
                createMapping(subTable,vpn+pages,prevppn+pages,bigPageSize-pages,prevPerm,level-1); // pushdown
            }
            entry.setValid();entry.raw.ppn=addr2pn(reinterpret_cast<xlen_t>(subTable));
        }
        DBG(entry.print();)
        createMapping(subTable,vpn,ppn,pages,perm,level-1);
    }
}
void PageTable::print(pgtbl_t table,xlen_t vpnBase,xlen_t entrySize){
    assert(entrySize>0);
    for(int i=0;i<pageEntriesPerPage;i++){
        auto &entry=table[i];
        if(entry.isValid()){
            if(entry.isLeaf()){
                printf("%lx => %lx [%lx]\n",(xlen_t)i*entrySize+vpnBase,entry.ppn(),entrySize);
            } else {
                printf("%lx => PTNode@%lx\n",(xlen_t)i*entrySize+vpnBase,entry.ppn());
                print(entry.child(),i*entrySize+vpnBase,entrySize>>9);
            }
        }
    }
}
pgtbl_t PageTable::createPTNode(){
    // return reinterpret_cast<pgtbl_t>(aligned_alloc(pageSize,pageSize));
    // auto rt=reinterpret_cast<pgtbl_t>(new PageTableNode);
    auto rt=reinterpret_cast<pgtbl_t>(vm::pn2addr(kernelPmgr->alloc(1)));
    printf("createPTNode=0x%lx\n",rt);
    return rt;
}