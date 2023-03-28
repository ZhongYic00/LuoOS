#include "types.h"

namespace vm
{
    struct PageTableEntry{
        
            xlen_t perm:9;
            // struct {
            // #define ONE(x) xlen_t x:1
            // ONE(v);
            // ONE(r);
            // ONE(w);
            // ONE(x);
            // ONE(u);
            // ONE(g);
            // ONE(a);
            // ONE(d);
            // };
        
        
        xlen_t _rsw:2;
        xlen_t ppn0:9;
        xlen_t ppn1:9;
        xlen_t ppn2:9;
    };
    constexpr xlen_t pageSize=0x1000;
    typedef PageTableEntry* pgtbl_t;
    class PageTable{
    private:
        pgtbl_t root;
    public:
        PageTable(pgtbl_t root){
            this->root=root;
        }
        void createMapping(xlen_t va,xlen_t pa,xlen_t pages){
            xlen_t vpn[3],ppn[3];
            for(int i=0;i<3;i++){
                ppn[i]=pa&0x1ff;
                vpn[i]=va&0x1ff;
                pa>>=9,va>>=9;
            }
        }
    };
} // namespace vm
