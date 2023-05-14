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
    typedef klib::pair<xlen_t,xlen_t> segment_t;
    // struct segment_t:public klib::pair<xlen_t,xlen_t>{
    //     segment_t(xlen_t start,xlen_t end):pair({start,end}){}
    //     segment_t(){}
    //     inline xlen_t size(){return second-first;}
    // };

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
        enum fieldOffsets{
            vOff,rOff,wOff,xOff,uOff,gOff,aOff,dOff,
        };
        enum fieldMasks{
            #define Off2Mask(field) field=1<<fieldOffsets::field##Off
            Off2Mask(v),Off2Mask(r),Off2Mask(w),Off2Mask(x),Off2Mask(u),Off2Mask(g),Off2Mask(a),Off2Mask(d)
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
    constexpr xlen_t vaddrOffsetMask=pageSize-1;
    inline xlen_t addr2offset(xlen_t addr){ return addr&(vaddrOffsetMask); }
    inline xlen_t bytes2pages(xlen_t bytes){ return bytes/pageSize+((bytes%pageSize)>0); }
    
    class alignas(pageSize) PageTableNode{
    private:
        uint8_t page[pageSize];
    public:
        PageTableNode(){
            memset(page,0,sizeof(page));
        }
    };

    typedef klib::pair<klib::pair<PageNum,PageNum>,PageNum> PageMapping;
    class VMO{
    public:
        enum class CloneType:uint8_t{
            clone,alloc,
        };
        const perm_t perm;
        const CloneType cloneType;
        const PageMapping mapping;
        VMO(const PageMapping &mapping,perm_t perm,CloneType cloneType=CloneType::clone):mapping(mapping),perm(perm),cloneType(cloneType){}
    private:
    };

    class PageTable{
    private:
        pgtbl_t root;
        static pgtbl_t createPTNode();
    public:
        inline PageTable(pgtbl_t root=nullptr){
            if(root==nullptr)this->root=createPTNode();
            else this->root=root;
        }
        inline PageTable(std::initializer_list<VMO> vmos,pgtbl_t root=nullptr):PageTable(root){
            for(auto vmo:vmos){
                switch(vmo.cloneType){
                    case VMO::CloneType::clone:
                        createMapping(vmo.mapping,vmo.perm); break;
                    default:
                        ;
                }
            }
        }
        inline ptr_t getRoot(){ return root; }
        void createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm,int level=2);
        inline void createMapping(PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm){
            createMapping(root,vpn,ppn,pages,perm);
        }
        inline void createMapping(const PageMapping &mapping,perm_t perm){
            createMapping(mapping.first.first,mapping.first.second,mapping.second,perm);
        }
        inline void createMapping(const VMO &vmo){createMapping(vmo.mapping,vmo.perm);}
        PageNum trans(PageNum vpn);
        inline xlen_t transaddr(xlen_t addr){
            return pn2addr(trans(addr2pn(addr)))+addr2offset(addr);
        }
        void print(pgtbl_t table,xlen_t vpnBase,xlen_t entrySize);
        inline void print(){
            printf("PageTable::print(root=%p)\n",root);
            print(root,0l,1l<<18);
        }
        static xlen_t toSATP(PageTable &table);
    };
    class VMAR{
    public:
        // @todo check initialize order
        inline VMAR(const std::initializer_list<VMO> &vmos,pgtbl_t root=nullptr):vmos(vmos),pagetable(vmos,root){}
        inline VMAR(const VMAR &other):vmos(other.vmos),pagetable(){}
        inline void alloc(PageNum vpn,PageNum pages,perm_t perm){
            PageNum ppn=0l;
            VMO vmo((PageMapping){{vpn,ppn},pages},perm);
            vmos.push_back(vmo);
            pagetable.createMapping(vmo);
        }
        inline void map(const VMO& vmo){
            vmos.push_back(vmo);
            pagetable.createMapping(vmo);
        }
        inline void map(PageNum vpn,PageNum ppn,PageNum pages,perm_t perm){map(VMO({{vpn,ppn},pages},perm));}
        inline void unmap();
        inline xlen_t satp(){return PageTable::toSATP(pagetable);}
        inline klib::ByteArray copyin(xlen_t addr,size_t len){
            xlen_t paddr=pagetable.transaddr(addr);
            klib::ByteArray buff((uint8_t*)paddr,len);
            return buff;
        }
        inline void print(){
            pagetable.print();
        }
    private:
        klib::list<VMO> vmos;
        PageTable pagetable;
    };

} // namespace vm
#endif