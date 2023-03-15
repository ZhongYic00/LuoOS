#define REPFIELD(name) u##name, s##name,h##name,m##name,

namespace csr
{
    namespace mie
    {
        enum fields{
            REPFIELD(sie)
            REPFIELD(tie)
            REPFIELD(eie)
        };
    } // namespace mie
    namespace mcause{
        enum interrupts{
            REPFIELD(si)
            REPFIELD(ti)
            REPFIELD(ei)
        };
        enum exceptions{
            breakpoint=3,
            storeAccessFault=7,
            uecall=8,
            secall=9,
        };
    }
    namespace mstatus{
        enum fields{
            REPFIELD(ie)
        };
    }
} // namespace csr
