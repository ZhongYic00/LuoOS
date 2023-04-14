#include "../include/common.h"
#include "klib.hh"
#include <bits/stdc++.h>

#define DEBUG 0

namespace vm
{
    // struct alignas(4096) Page{char raw[4096];};
    union PageTableEntry;
    typedef PageTableEntry* pgtbl_t;
    typedef xlen_t PageNum;
    inline xlen_t pn2addr(xlen_t pn){ return pn<<12; }
    inline xlen_t addr2pn(xlen_t addr){ return addr>>12; }
    union PageTableEntry{
        struct Fields{
        #define ONE(x) int x:1
            ONE(v);
            ONE(r);
            ONE(w);
            ONE(x);
            ONE(u);
            ONE(g);
            ONE(a);
            ONE(d);
            xlen_t _rsw:2;
            xlen_t ppn0:9,ppn1:9,ppn2:26;
        }fields;
        struct Raw{
            xlen_t perm:8;
            xlen_t _rsw:2;
            xlen_t ppn:44;
        }raw;
        enum fields{
            v,r,w,x,u,g,a,d,
        };
        inline bool isValid(){ return fields.v; }
        inline void setValid(){ fields.v=1; }
        inline void setPTNode(){ fields.r=fields.w=fields.x=0; }
        inline bool isLeaf(){ return fields.r|fields.w|fields.x; }
        inline xlen_t ppn(){ return raw.ppn; }
        inline pgtbl_t child(){ return reinterpret_cast<pgtbl_t>( pn2addr(ppn()) ); }
        inline void print(){
            printf("[%08lx] %c%c%c%c\n",raw.ppn,fields.r?'r':'-',fields.w?'w':'-',fields.x?'x':'-',fields.v?'v':'-');
        }
    };
    
    constexpr xlen_t pageSize=0x1000,
        pageEntriesPerPage=pageSize/sizeof(PageTableEntry);
    constexpr xlen_t vpnMask=0x1ff;
    
    class alignas(pageSize) PageTableNode{
    private:
        uint8_t page[pageSize];
    public:
        PageTableNode(){
            memset(page,0,sizeof(page));
        }
    };
    class PageTable{
    private:
        pgtbl_t root;
        pgtbl_t createPTNode(){
            return reinterpret_cast<pgtbl_t>(aligned_alloc(pageSize,pageSize));
            // return reinterpret_cast<pgtbl_t>(new PageTableNode);
        }
    public:
        PageTable(pgtbl_t root){
            this->root=root;
        }
        inline void createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,int level=2){
            xlen_t bigPageSize=1l<<(9*level);
            xlen_t unaligned=vpn&(bigPageSize-1);
            DBG(
                printf("createMapping(table,vpn=0x%lx,ppn=0x%lx,pages=0x%lx,level=%d)\n",vpn,ppn,pages,level);
                printf("bigPageSize=%lx,unaligned=%lx\n",bigPageSize,unaligned);
            )
            // align vpn to boundary
            if(unaligned){
                auto partial=std::min(bigPageSize-unaligned,pages);
                PageTableEntry &entry=table[(vpn/bigPageSize)&vpnMask];
                pgtbl_t subTable=nullptr;
                if(entry.isValid() && !entry.isLeaf()){ // if the range is previously managed, and is not bigPage
                    subTable=entry.child();
                }
                else{
                    subTable=createPTNode();
                    if(entry.isValid() && entry.isLeaf()){ // if is bigPage, change to ptnode and pushdown
                        xlen_t prevppn=entry.ppn();
                        entry.setPTNode();
                        createMapping(subTable,vpn-unaligned,prevppn,unaligned,level-1);
                    }
                    entry.setValid();entry.raw.ppn=addr2pn(reinterpret_cast<xlen_t>(subTable));
                }
                DBG(entry.print();)
                DBG(printf("subtable=%lx\n",subTable);)
                createMapping(subTable,vpn,ppn,partial,level-1);
                vpn+=partial,ppn+=partial,pages-=partial;
            }
            // map aligned whole pages
            for(int i=(vpn/bigPageSize)&vpnMask;pages>=bigPageSize;i++){
                // create big page entry
                PageTableEntry &entry=table[i];
                if(entry.isValid()){} // remapping a existing vaddr
                entry.setValid();
                entry.fields.r=1,entry.fields.w=1,entry.fields.x=1;
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
                        entry.setPTNode();
                        createMapping(subTable,vpn+pages,prevppn+pages,bigPageSize-pages,level-1);
                    }
                    entry.setValid();entry.raw.ppn=addr2pn(reinterpret_cast<xlen_t>(subTable));
                }
                DBG(entry.print();)
                createMapping(subTable,vpn,ppn,pages,level-1);
            }
        }
        inline void createMapping(PageNum vpn,PageNum ppn,xlen_t pages){
            createMapping(root,vpn,ppn,pages);
        }
        inline void print(pgtbl_t table,xlen_t vpnBase=0,xlen_t entrySize=1l<<18){
            assert(entrySize>0);
            for(int i=0;i<pageEntriesPerPage;i++){
                auto &entry=table[i];
                if(entry.isValid()){
                    if(entry.isLeaf()){
                        printf("%lx => %lx [%lx]\n",i*entrySize+vpnBase,entry.ppn(),entrySize);
                    } else {
                        printf("%lx => PTNode@%lx\n",i*entrySize+vpnBase,entry.ppn());
                        print(entry.child(),i*entrySize+vpnBase,entrySize>>9);
                    }
                }
            }
        }
        inline void print(){
            print(root);
        }
    };
} // namespace vm
using namespace vm;

pgtbl_t root[pageEntriesPerPage];
int main(){
    PageTable table(reinterpret_cast<pgtbl_t>(root));
    table.createMapping(0x80000,0x8e120,0x1000);
    table.createMapping(0x80000,0x8e000,1);
    table.print();
    table.createMapping(0x80000,0x8f000,0x800);
    table.print();
    std::list<int> l;
}