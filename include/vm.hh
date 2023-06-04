#ifndef VM_HH__
#define VM_HH__

#include "common.h"
#include "klib.hh"
#include "TINYSTL/vector.h"

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

    inline constexpr xlen_t pn2addr(xlen_t pn){ return pn<<12; }
    inline constexpr xlen_t addr2pn(xlen_t addr){ return addr>>12; }
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
        inline void setInvalid(){ raw.perm=0; }
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
    inline constexpr xlen_t addr2offset(xlen_t addr){ return addr&(vaddrOffsetMask); }
    inline constexpr xlen_t bytes2pages(xlen_t bytes){ return bytes/pageSize+((bytes%pageSize)>0); }
    inline constexpr xlen_t ceil(xlen_t addr){ return bytes2pages(addr)*pageSize; }
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
        static VMO alloc(PageNum pages,CloneType=CloneType::clone);
        bool operator==(const VMO& other){return ppn_==other.ppn_&&pages_==other.pages_;}
    private:
    };

    struct PageMapping{
        enum Prot{
            none=0x0,read=0x4,write=0x2,exec=0x1
        };
        enum class MappingType:uint8_t{
            normal=0x0,file=0x1,anon=0x2
        };
        enum class SharingType:uint8_t{
            /// @brief changes aren't visible to others, copy on write
            privt=0x0,
            /// @brief not implemented
            copy=0x1,
            /// @brief changes are shared
            shared=0x2
        };
        PageNum vpn;
        VMO vmo;
        const perm_t perm;
        const MappingType mapping=MappingType::normal;
        const SharingType sharing=SharingType::privt;
        inline PageNum ppn() const{return vmo.ppn();}
        inline PageNum pages() const{return vmo.pages();}
        inline klib::string toString() const{return klib::format("%lx=>%s",vpn,vmo.toString());}
        inline PageMapping clone() const{return PageMapping{vpn,vmo.clone(),perm};}
        inline static perm_t prot2perm(Prot prot){
            perm_t rt=0;
            using masks=PageTableEntry::fieldMasks;
            if(prot&read)rt|=masks::r;
            if(prot&write)rt|=masks::w;
            if(prot&exec)rt|=masks::x;
            return rt;
        }
        inline bool contains(xlen_t addr){
            return addr>=pn2addr(vpn) && addr<=pn2addr(vpn+vmo.pages());
        }
        bool operator==(const PageMapping &other){return vpn==other.vpn&&vmo==other.vmo&&perm==other.perm&&mapping==other.mapping&&sharing==other.sharing;}
    };

    class PageTable{
    private:
        pgtbl_t root;
        static pgtbl_t createPTNode();
        static bool freePTNode(pgtbl_t);
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
        void removeMapping(pgtbl_t table,PageNum vpn,xlen_t pages,int level=2);
        inline void removeMapping(const PageMapping &mapping){
            removeMapping(root,mapping.vpn,mapping.pages());
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
        inline void reset(){
            Log(debug,"before reset, VMAR:%s",mappings.toString().c_str());
            tinystl::vector<PageMapping> toremove;
            for(auto mapping:mappings){
                if(mapping.mapping!=PageMapping::MappingType::normal){
                    Log(info,"unmap %s",mapping.toString().c_str());
                    toremove.push_back(mapping);
                    pagetable.removeMapping(mapping);
                }
            }
            /// @todo better efficiency
            for(auto &mapping:toremove)
                mappings.remove(mapping);
            Log(debug,"after reset, VMAR:%s",mappings.toString().c_str());
        }
        inline xlen_t satp(){return PageTable::toSATP(pagetable);}
        // @todo @bug what if region is on border?
        inline klib::ByteArray copyinstr(xlen_t addr, size_t len) {
            // @todo 检查用户源是否越界（addr+len来自用户进程大小之外的空间）
            xlen_t paddr = pagetable.transaddr(addr);
            char buff[len];
            strncpy(buff, (char*)paddr, len);
            return klib::ByteArray((uint8_t*)buff, strlen(buff)+1);
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
        inline bool contains(xlen_t addr){
            for(auto mapping: mappings){
                if(mapping.contains(addr))return true;
            }
            return false;
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
            template<typename T>
            void operator>>(T &d){
                auto buf=parent.copyin(vaddr,sizeof(T));
                d=*reinterpret_cast<T*>(buf.buff);
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