#ifndef VM_HH__
#define VM_HH__

#include "common.h"
#include "klib.hh"
#include <EASTL/set.h>

// #define moduleLevel LogLevel::info

namespace vm
{
    // struct alignas(4096) Page{char raw[4096];};
    union PageTableEntry;
    typedef PageTableEntry *pgtbl_t;
    typedef xlen_t PageNum;
    typedef uint8_t perm_t;
    typedef klib::pair<xlen_t,xlen_t> segment_t;
    using Segment=::klib::Segment<PageNum>;
    using eastl::tuple;
    typedef tuple<PageNum,PageNum,PageNum> PageSlice;

    inline constexpr xlen_t pn2addr(xlen_t pn){ return pn<<12; }
    inline constexpr xlen_t addr2pn(xlen_t addr){ return addr>>12; }
    union PageTableEntry{
        struct Fields{
        #define ONE(x) int x:1
            ONE(v);ONE(r);ONE(w);ONE(x);ONE(u);ONE(g);ONE(a);ONE(d);
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
        inline bool isValid(){ assert(this);return fields.v; }
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
    struct PageBuf;
    typedef Arc<PageBuf> PBufRef;
    class Pager{
    public:
        virtual PBufRef load(PageNum offset)=0;
        virtual void put(PBufRef)=0;
    };
    class VMO
    {
    public:
        virtual ~VMO(){}
        virtual PageNum len() const=0;
        virtual klib::string toString() const=0;
        /// @brief create a shallow copy of [start,end], pages are shared
        /// @param start @param end relative pagenum
        // virtual Arc<VMO> shallow(PageNum start,PageNum end);
        // inline Arc<VMO> shallow(){return shallow(0,len());}
        
        /// @brief create a deep copy, pages are copied
        virtual Arc<VMO> clone() const=0;
        virtual PageSlice req(PageNum offset)=0;
    };
    struct PageMapping{
        enum Prot{
            none=0x0,read=0x1,write=0x2,exec=0x4,mask=0xf
        };
        enum class MappingType:uint8_t{
            system=0x0,file=0x1,anon=0x2
        };
        enum class SharingType:uint8_t{
            /// @brief changes aren't visible to others, copy on write
            privt=0x0,
            /// @brief changes are shared
            shared=0x1
        };
        const PageNum vpn;
        const PageNum len;
        const PageNum offset;
        Arc<VMO> vmo;
        const perm_t perm;
        const MappingType mapping = MappingType::anon;
        const SharingType sharing = SharingType::privt;
        inline PageNum pages() const{return len;}
        inline PageNum vend() const { return vpn + pages() - 1; }
        inline klib::string toString() const { return klib::format("%lx=>%s", vpn, vmo->toString()); }
        /// @param region absolute vpn region
        inline PageMapping splitChild(Segment region) const {
            /// @todo reduce mem
            return PageMapping{region.l,region.length(),offset,vmo,perm,mapping,sharing};
        }
        inline PageSlice req(PageNum idx) const{return vmo->req(offset+idx);}
        inline PageMapping clone() const {
            auto rt=*this;
            if(sharing==SharingType::shared);
            else rt.vmo=vmo->clone();   // copy existing
            return rt;
        }
        inline static perm_t prot2perm(Prot prot){
            using masks=PageTableEntry::fieldMasks;
            perm_t rt=masks::v|masks::u;
            rt|=(prot&mask)<<1;
            return rt;
        }
        inline bool contains(xlen_t addr) const {
            return addr>=pn2addr(vpn) && addr<=pn2addr(vpn+vmo->len());
        }
        inline Segment vsegment() const{return Segment{vpn,vend()};}
        bool operator==(const PageMapping &other) const { return vpn == other.vpn && vmo == other.vmo && perm == other.perm && mapping == other.mapping && sharing == other.sharing; }
        bool operator<(const PageMapping &other) const { return vpn < other.vpn; }
        Segment operator&(const PageMapping &other) const { return vsegment()&other.vsegment(); }
    };
    class VMOMapper{
        Segment region;
    public:
        VMOMapper(Arc<VMO> vmo);
        ~VMOMapper();
        inline xlen_t start(){return pn2addr(region.l);}
        inline klib::ByteArray asBytes(){return klib::ByteArray((uint8_t*)pn2addr(region.l),region.length()*pageSize);}
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
        inline ptr_t getRoot(){ return root; }
        void createMapping(pgtbl_t table,PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm,int level=2);
        inline void createMapping(PageNum vpn,PageNum ppn,xlen_t pages,perm_t perm){
            createMapping(root,vpn,ppn,pages,perm);
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
            Log(info,"%s",toString().c_str());
        }
        static xlen_t toSATP(PageTable &table);
    };
    class VMAR{
    public:
        // @todo check initialize order
        inline VMAR(const std::initializer_list<PageMapping> &mappings,pgtbl_t root=nullptr):mappings(mappings),pagetable(root){for(auto &mapping:mappings)map(mapping);}
        inline VMAR(const VMAR &other):pagetable(){
            for(const auto &mapping:other.mappings)map(mapping.clone());
        }
        inline auto find(xlen_t addr){
            auto vpn=addr2pn(addr);
            auto mapping=mappings.upper_bound(PageMapping{vpn});
            if(mapping!=mappings.begin())mapping--;
            if(!mapping->contains(addr))return mappings.end();
            else return mapping;
        }
        inline void pfhandler(xlen_t addr){
            // find last elem <= vpn
            auto vpn=addr2pn(addr);
            auto mapping=find(addr);
            assert(mapping!=mappings.end());
            auto [off,ppn,pages]=mapping->req(vpn-mapping->vpn);
            auto voff=off-mapping->offset;
            pagetable.createMapping(mapping->vpn+voff,ppn,pages,mapping->perm);
        }
        /// @brief overlap should has been unmapped
        void map(const PageMapping &mapping,bool ondemand=false);
        /// @param region absolute vpn
        inline void unmap(const Segment region){
            // Log(debug, "unmap %s", mapping.toString().c_str());
            // find left
            // auto l=mappings.lower_bound();if(l!=mappings.begin())l--;
            // bf
            auto l = mappings.begin();
            auto r = mappings.end();
            for (auto i = l; i != r;){
                if (auto ovlp=i->vsegment() & region){
                    // left part
                    if(auto lregion=Segment{i->vpn,ovlp.l-1}){
                        auto lslice=i->splitChild(lregion);
                        map(lslice);
                    }
                    // right part
                    if(auto rregion=Segment{ovlp.r+1,i->vend()}){
                        auto rslice=i->splitChild(rregion);
                        map(rslice);
                    }
                    // remove origin
                    /// @bug may be incorrect
                    pagetable.removeMapping(*i);
                    i=mappings.erase(i);
                } else i++;
            }
            Log(debug, "after unmap, VMAR:%s", klib::toString(mappings).c_str());
        }
        /// @brief clear all mmaps
        void reset();
        inline xlen_t satp(){return PageTable::toSATP(pagetable);}
        // @todo @bug what if region is on border?
        inline klib::ByteArray copyinstr(xlen_t addr, size_t len) {
            // @todo 检查用户源是否越界（addr+len来自用户进程大小之外的空间）
            xlen_t paddr = pagetable.transaddr(addr);
            auto buf=klib::ByteArray(len+1);
            strncpy((char*)buf.buff, (char*)paddr, len);
            buf.buff[len]='\0';
            return buf;
        }
        inline klib::ByteArray copyin(xlen_t addr,size_t len){
            // @todo 检查用户源是否越界（addr+len来自用户进程大小之外的空间）
            xlen_t paddr=pagetable.transaddr(addr);
            auto buff=klib::ByteArray::from(paddr,len);
            return buff;
        }
        inline void copyout(xlen_t addr,const klib::ByteArray &buff){
            // @todo 检查拷贝后是否会越界（addr+buff.len后超出用户进程大小）
            // xlen_t paddr=pagetable.transaddr(addr);
            auto mapping=find(addr);
            VMOMapper mapper(mapping->vmo);
            auto off=addr-pn2addr(mapping->vpn)+pn2addr(mapping->offset);
            memmove((ptr_t)mapper.start()+off,buff.buff,buff.len);
        }
        inline bool contains(xlen_t addr){
            for(auto mapping: mappings){
                if(mapping.contains(addr))return true;
            }
            return false;
        }
        template<typename Lambda>
        inline PageMapping find(Lambda predicate){
            for(auto mapping: mappings){
                if(predicate(mapping))
                    return mapping;
            }
            /// @todo error handling
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
            Log(info,"Mappings:\t%s",klib::toString(mappings).c_str());
            pagetable.print();
        }
    private:
        // klib::list<VMAR> children;
        eastl::set<PageMapping> mappings;
        PageTable pagetable;
    };

} // namespace vm
#endif