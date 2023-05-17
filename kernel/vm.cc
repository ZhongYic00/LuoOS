#include "vm.hh"
#include "alloc.hh"
#include "kernel.hh"
#include "rvcsr.hh"
#define DEBUG 1

using namespace vm;
PageNum PageTable::trans(PageNum vpn){
    xlen_t bigPageSize=1l<<(9*2);
    for(pgtbl_t table=root;;bigPageSize>>=9){
        PageTableEntry &entry=table[(vpn/bigPageSize)&vpnMask];
        assert(entry.isValid());
        if(entry.isLeaf())
            return entry.ppn()+(vpn&(bigPageSize-1));
        else
            table=entry.child();
    }
}
#define DBG_ENTRY DBG(\
            entry.print();\
            assert(entry.isValid());\
        )
void PageTable::createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,const perm_t perm,int level){
    xlen_t bigPageSize=1l<<(9*level);
    xlen_t unaligned=vpn&(bigPageSize-1);
    DBG(
        printf("createMapping(table,vpn=0x%lx,ppn=0x%lx,pages=0x%lx,perm=%x,level=%d)\n",vpn,ppn,pages,perm,level);
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
        DBG_ENTRY
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
        DBG_ENTRY;

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
        DBG_ENTRY
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
    auto rt=reinterpret_cast<pgtbl_t>(vm::pn2addr(kGlobObjs.pageMgr->alloc(1)));
    printf("createPTNode=0x%lx\n",rt);
    return rt;
}
xlen_t PageTable::toSATP(PageTable &table){
    csr::satp satp;
    satp.mode=8;
    satp.asid=0;
    satp.ppn=vm::addr2pn((xlen_t)(table.getRoot()));
    return satp.value();
}

VMO vm::VMO::clone() const{
    PageMapping mapping=this->mapping;
    PageNum &ppn=mapping.first.second,pages=mapping.second;
    switch(cloneType){
        case CloneType::shared:
            break;
        case CloneType::clone:
            ppn=kGlobObjs.pageMgr->alloc(pages);
            copyframes(this->ppn(),ppn,pages);
            break;
        case CloneType::alloc:
            ppn=kGlobObjs.pageMgr->alloc(pages);
            break;
    }
    return VMO(mapping,perm,cloneType);
}