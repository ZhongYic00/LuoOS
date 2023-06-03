#include "vm.hh"
#include "alloc.hh"
#include "kernel.hh"
#include "rvcsr.hh"

#define moduleLevel LogLevel::info

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
            Log(debug,"%s",entry.toString().c_str());\
            assert(entry.isValid());\
        )
void PageTable::createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,const perm_t perm,int level){
    xlen_t bigPageSize=1l<<(9*level);
    xlen_t unaligned=vpn&(bigPageSize-1);
    Log(debug,"createMapping(table,vpn=0x%lx,ppn=0x%lx,pages=0x%lx,perm=%x,level=%d)\n",vpn,ppn,pages,perm,level);
    Log(debug,"bigPageSize=%lx,unaligned=%lx\n",bigPageSize,unaligned);
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
        Log(debug,"subtable=%lx\n",subTable);
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
void PageTable::removeMapping(pgtbl_t table,PageNum vpn,xlen_t pages,int level){
    xlen_t bigPageSize=1l<<(9*level);
    xlen_t unaligned=vpn&(bigPageSize-1);
    Log(debug,"removeMapping(table,vpn=0x%lx,pages=0x%lx,level=%d)\n",vpn,pages,level);
    Log(debug,"bigPageSize=%lx,unaligned=%lx\n",bigPageSize,unaligned);
    // align vpn to boundary
    if(unaligned){
        auto partial=klib::min(bigPageSize-unaligned,pages);
        PageTableEntry &entry=table[(vpn/bigPageSize)&vpnMask];
        assert(entry.isValid() && !entry.isLeaf());
        pgtbl_t subTable=entry.child();
        DBG_ENTRY
        Log(debug,"subtable=%lx\n",subTable);
        removeMapping(subTable,vpn,partial,level-1); // actual create mapping
        if(freePTNode(subTable))entry.setInvalid();
        vpn+=partial,pages-=partial;
    }
    // unmap aligned whole pages
    for(int i=(vpn/bigPageSize)&vpnMask;pages>=bigPageSize;i++){
        PageTableEntry &entry=table[i];
        assert(entry.isValid());
        // big page entry
        if(entry.isLeaf())entry.setInvalid();
        else {
            // pushed down
            removeMapping(entry.child(),vpn,pages,level-1);
            assert(freePTNode(entry.child()));
            entry.setInvalid();
        }

        vpn+=bigPageSize;
        pages-=bigPageSize;
    }
    // unmap rest pages
    if(pages){
        assert(pages>0);
        PageTableEntry &entry=table[(vpn/bigPageSize)&vpnMask];
        assert(entry.isValid() && !entry.isLeaf());
        pgtbl_t subTable=entry.child();
        DBG_ENTRY
        Log(debug,"subtable=%lx\n",subTable);
        removeMapping(subTable,vpn,pages,level-1); // actual create mapping
        if(freePTNode(subTable))entry.setInvalid();
    }
}
klib::string PageTable::toString(pgtbl_t table,xlen_t vpnBase,xlen_t entrySize){
    assert(entrySize>0);
    klib::string s;
    for(int i=0;i<pageEntriesPerPage;i++){
        auto &entry=table[i];
        if(entry.isValid()){
            if(entry.isLeaf()){
                s+=entry.toString((xlen_t)i*entrySize+vpnBase,entrySize)+",";
            } else {
                s+=klib::format("%s:\t[%s]\t,",entry.toString((xlen_t)i*entrySize+vpnBase).c_str(),toString(entry.child(),i*entrySize+vpnBase,entrySize>>9).c_str());
            }
        }
    }
    return s;
}
pgtbl_t PageTable::createPTNode(){
    // return reinterpret_cast<pgtbl_t>(aligned_alloc(pageSize,pageSize));
    // auto rt=reinterpret_cast<pgtbl_t>(new PageTableNode);
    auto rt=reinterpret_cast<pgtbl_t>(vm::pn2addr(kGlobObjs.pageMgr->alloc(1)));
    Log(debug,"createPTNode=0x%lx",rt);
    return rt;
}
bool PageTable::freePTNode(pgtbl_t table){
    for(int i=0;i<pageEntriesPerPage;i++){
        auto &entry=table[i];
        if(entry.isValid())return false;
    }
    kGlobObjs.pageMgr->free(addr2pn((xlen_t)table),0);
    return true;
}
xlen_t PageTable::toSATP(PageTable &table){
    csr::satp satp;
    satp.mode=8;
    satp.asid=0;
    satp.ppn=vm::addr2pn((xlen_t)(table.getRoot()));
    return satp.value();
}

VMO vm::VMO::clone() const{
    PageNum ppn=this->ppn(),pages=this->pages();
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
        default:
            panic("unknown cloneType!");
    }
    return VMO{ppn,pages,cloneType};
}
VMO vm::VMO::alloc(PageNum pages,CloneType type){
    PageNum ppn=kGlobObjs.pageMgr->alloc(pages);
    return VMO{ppn,pages};
}