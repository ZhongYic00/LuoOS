#ifndef VM_HH__
#define VM_HH__

#include "common.h"
#include "klib.hh"

namespace vm
{
    // struct alignas(4096) Page{char raw[4096];};
    union PageTableEntry;
    typedef PageTableEntry* pgtbl_t;
    typedef xlen_t PageNum;
    typedef uint8_t perm_t;
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
        inline perm_t perm(){ return raw.perm; }
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
        pgtbl_t createPTNode();
    public:
        inline PageTable(pgtbl_t root){
            this->root=root;
        }
        void createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm,int level=2);
        inline void createMapping(PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm){
            createMapping(root,vpn,ppn,pages,perm);
        }
        void print(pgtbl_t table,xlen_t vpnBase=0,xlen_t entrySize=1l<<18);
        inline void print(){
            print(root);
        }
    };
} // namespace vm
#endif