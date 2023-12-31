#ifndef LD_HH__
#define LD_HH__
#include "common.h"
#include "vm.hh"
#include "fs.hh"
#include <elf.h>

namespace ld
{
    namespace elf
    {
        using vm::perm_t;
        inline perm_t flags2perm(Elf64_Word flags){
            using mask=vm::PageTableEntry::fieldMasks;
            return ((flags&0x1)?mask::x:0)|((flags&0x2)?mask::w:0)|((flags&0x4)?mask::r:0)|mask::v|mask::u;
            // return mask::r|mask::w|mask::x|mask::v|mask::u;
        }
    } // namespace elf
    eastl::tuple<xlen_t,xlen_t> loadElf(const uint8_t *buff,vm::VMAR &vmar);
    struct ElfInfo{
        addr_t phdr;
        Elf64_Addr  e_entry;
        Elf64_Half	e_phentsize;
        Elf64_Half	e_phnum;
    };
    /// @brief 
    /// @return [entry, program break, ]
    eastl::tuple<xlen_t,xlen_t,ElfInfo> loadElf(shared_ptr<fs::File> file,vm::VMAR &vmar,addr_t base=0);
    bool isElf(shared_ptr<fs::File> file);
} // namespace ld

#endif