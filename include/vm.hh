#ifndef VM_HH__
#define VM_HH__

#include "common.h"
#include "klib.hh"

#define DEBUG 1
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
    inline xlen_t copyframes(PageNum src,PageNum dst,PageNum pages){
        memcpy((ptr_t)pn2addr(dst),(ptr_t)pn2addr(src),pages*pageSize);
    }
    
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
            clone,alloc,shared,
        };
        const perm_t perm;
        const CloneType cloneType;
        
        /**
         * @brief {{vpn,ppn},pages}
         */
        const PageMapping mapping;
        VMO(const PageMapping &mapping,perm_t perm,CloneType cloneType=CloneType::clone):mapping(mapping),perm(perm),cloneType(cloneType){}
        VMO clone() const;
        inline PageNum pages() const{return mapping.second;}
        inline PageNum vpn() const{return mapping.first.first;}
        inline PageNum ppn() const{return mapping.first.second;}
        inline void print() const{printf("VMO[0x%lx=>0x%lx@%lx]\n",vpn(),ppn(),pages());}
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
        using CloneType=VMO::CloneType;
        // @todo check initialize order
        inline VMAR(const std::initializer_list<VMO> &vmos,pgtbl_t root=nullptr):vmos(vmos),pagetable(vmos,root){}
        inline VMAR(const VMAR &other):vmos(),pagetable(){
            for(const auto &vmo:other.vmos)map(vmo.clone());
        }
        inline void alloc(PageNum vpn,PageNum pages,perm_t perm){
            PageNum ppn=0l;
            VMO vmo((PageMapping){{vpn,ppn},pages},perm);
            vmos.push_back(vmo);
            pagetable.createMapping(vmo);
        }
        /**
         * @todo 创建第一个线程loadElf时，0x83002的1page映射之后链表里找不到，log为：
         * map VMO[0x0000000000083002=>0x0000000000083fea@0000000000000001]
         * after map, VMAR:VMO[0x0000000000080200=>0x0000000000080200@000000000000000a]
         * VMO[0x000000000008020a=>0x000000000008020a@0000000000000001]
         * VMO[0x000000000008020e=>0x000000000008020e@0000000000000009]
         * VMO[0x000000000008020b=>0x000000000008020b@0000000000000001]
         * VMO[0x0000000000080217=>0x0000000000080217@0000000000000030]
         * VMO[0x000000000007ffff=>0x0000000000083ffb@0000000000000001]
         * VMO[0x0000000000080216=>0x0000000000083ffe@0000000000000001]
         * VMO[0x0000000000083000=>0x0000000000083fe7@0000000000000002]
         * */
        inline void map(const VMO& vmo){
            DBG(printf("map ");vmo.print();)
            vmos.push_back(vmo);
            pagetable.createMapping(vmo);
            DBG(printf("after map, VMAR:");print();)
        }
        inline void map(PageNum vpn,PageNum ppn,PageNum pages,perm_t perm,CloneType ct=CloneType::clone){map(VMO({{vpn,ppn},pages},perm,ct));}
        inline void unmap();
        inline xlen_t satp(){return PageTable::toSATP(pagetable);}
        inline klib::ByteArray copyin(xlen_t addr,size_t len){
            xlen_t paddr=pagetable.transaddr(addr);
            klib::ByteArray buff((uint8_t*)paddr,len);
            return buff;
        }
        inline void print(){
            // pagetable.print();
            for(auto vmo:vmos)vmo.print();
        }
    private:
        klib::list<VMO> vmos;
        PageTable pagetable;
    };

} // namespace vm
#endif