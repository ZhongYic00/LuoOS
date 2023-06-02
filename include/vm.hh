#ifndef VM_HH__
#define VM_HH__

#include "common.h"
#include "klib.hh"

#define moduleLevel LogLevel::debug
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
        inline klib::string toString(){
            return klib::format("[%lx] %c%c%c%c",(xlen_t)raw.ppn,fields.r?'r':'-',fields.w?'w':'-',fields.x?'x':'-',fields.v?'v':'-');
        }
        inline klib::string toString(PageNum vpn,PageNum pages){
            return klib::format("%lx=>%lx[%x] %c%c%c%c",vpn,ppn(),pages,fields.r?'r':'-',fields.w?'w':'-',fields.x?'x':'-',fields.v?'v':'-');
        }
        inline klib::string toString(PageNum vpn){
            return klib::format("%lx => PTNode@%lx",vpn,ppn());
        }
    };
    
    constexpr xlen_t pageShift=12;
    constexpr xlen_t pageSize=1L<<pageShift;
    constexpr xlen_t pageEntriesPerPage=pageSize/sizeof(PageTableEntry);
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

    class VMO{
        const PageNum ppn_,pages_;
    public:
        enum class CloneType:uint8_t{
            clone,alloc,shared,
        };
        const CloneType cloneType;
        
        VMO(PageNum ppn,PageNum pages,CloneType cloneType=CloneType::clone):ppn_(ppn),pages_(pages),cloneType(cloneType){}
        VMO clone() const;
        inline PageNum pages() const{return pages_;}
        inline PageNum ppn() const{return ppn_;}
        inline klib::string toString() const{return klib::format("<VMO>@0x%lx[%lx]\n",ppn(),pages());}
        static VMO alloc(PageNum pages,CloneType type=CloneType::clone);
    private:
    };

    struct PageMapping{
        enum class MappingType:uint8_t{
            Normal,MMap,
        };
        PageNum vpn;
        VMO vmo;
        const perm_t perm;
        const MappingType type=MappingType::Normal;
        inline PageNum ppn() const{return vmo.ppn();}
        inline PageNum pages() const{return vmo.pages();}
        inline klib::string toString() const{return klib::format("%lx=>%s",vpn,vmo.toString());}
        inline PageMapping clone() const{return PageMapping{vpn,vmo.clone(),perm};}
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
        // inline PageTable(std::initializer_list<VMO> vmos,pgtbl_t root=nullptr):PageTable(root){
        //     for(auto vmo:vmos){
        //         switch(vmo.cloneType){
        //             case VMO::CloneType::clone:
        //                 createMapping(vmo.mapping,vmo.perm); break;
        //             default:
        //                 ;
        //         }
        //     }
        // }
        inline ptr_t getRoot(){ return root; }
        void createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm,int level=2);
        inline void createMapping(PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm){
            createMapping(root,vpn,ppn,pages,perm);
        }
        inline void createMapping(const PageMapping &mapping){
            createMapping(mapping.vpn,mapping.ppn(),mapping.pages(),mapping.perm);
        }
        PageNum trans(PageNum vpn);
        inline xlen_t transaddr(xlen_t addr){
            return pn2addr(trans(addr2pn(addr)))+addr2offset(addr);
        }
        klib::string toString(pgtbl_t table,xlen_t vpnBase,xlen_t entrySize);
        inline klib::string toString(){return toString(root,0l,1l<<18);}
        inline void print(){
            Log(debug,"PageTable::print(root=%p)\n",root);
            Log(trace,"%s",toString().c_str());
        }
        static xlen_t toSATP(PageTable &table);
    };
    class VMAR{
    public:
        using CloneType=VMO::CloneType;
        // @todo check initialize order
        inline VMAR(const std::initializer_list<PageMapping> &mappings,pgtbl_t root=nullptr):mappings(mappings),pagetable(root){for(auto &mapping:mappings)map(mapping);}
        inline VMAR(const VMAR &other):pagetable(){
            for(const auto &mapping:other.mappings)map(mapping.clone());
        }
        // inline void alloc(PageNum vpn,PageNum pages,perm_t perm){
        //     PageNum ppn=0l;
        //     // VMO vmo((PageMapping){{vpn,ppn},pages},perm);
        //     // vmos.push_back(vmo);
        //     // pagetable.createMapping(vmo);
        // }
        inline void map(const PageMapping& mapping){
            Log(debug,"map %s",mapping.toString().c_str());
            mappings.push_back(mapping);
            pagetable.createMapping(mapping);
            Log(debug,"after map, VMAR:%s",mappings.toString().c_str());
        }
        inline void map(PageNum vpn,PageNum ppn,PageNum pages,perm_t perm,CloneType ct=CloneType::clone){map(PageMapping{vpn,VMO{ppn,pages,ct},perm});}
        inline void unmap();
        inline xlen_t satp(){return PageTable::toSATP(pagetable);}
        // @todo @bug what if region is on border?
        inline klib::ByteArray copyinstr(xlen_t addr, size_t len) {
            // @todo 检查用户源是否越界（addr+len来自用户进程大小之外的空间）
            xlen_t paddr = pagetable.transaddr(addr);
            char buff[len];
            strncpy(buff, (char*)addr, len);
            return klib::ByteArray((uint8_t*)buff, strlen(buff));
        }
        inline klib::ByteArray copyin(xlen_t addr,size_t len){
            // @todo 检查用户源是否越界（addr+len来自用户进程大小之外的空间）
            xlen_t paddr=pagetable.transaddr(addr);
            klib::ByteArray buff((uint8_t*)paddr,len);
            return buff;
        }
        inline void copyout(xlen_t addr,const klib::ByteArray &buff){
            // @todo 检查拷贝后是否会越界（addr+buff.len后超出用户进程大小）
            xlen_t paddr=pagetable.transaddr(addr);
            memmove((ptr_t)paddr,buff.buff,buff.len);
        }
        class Writer{
            xlen_t vaddr;
            VMAR &parent;
        public:
            Writer(xlen_t addr,VMAR &parent):vaddr(addr),parent(parent){}
            template<typename T>
            inline void operator=(const T &d){
                auto buff=klib::ByteArray((uint8_t*)&d,sizeof(d));
                operator=(buff);
            }
            inline void operator=(const klib::ByteArray &bytes){
                parent.copyout(vaddr,bytes);
            }
        };
        inline Writer operator[](xlen_t vaddr){return Writer(vaddr,*this);}
        inline void print(){
            Log(trace,"Mappings:\t%s",mappings.toString().c_str());
            TRACE(pagetable.print();)
        }
    private:
        // klib::list<VMAR> children;
        klib::list<PageMapping> mappings;
        PageTable pagetable;
    };

} // namespace vm
#endif