#include "../include/ld.hh"
#include "../include/kernel.hh"
#include "vm/vmo.hh"
#include "vm/pager.hh"
#include <elf.h>

eastl::tuple<xlen_t,xlen_t> ld::loadElf(const uint8_t *buff,vm::VMAR &vmar){
    Elf64_Ehdr *elfHeader=(Elf64_Ehdr*)(buff);
    Log(info,"Elf header=%p, section header table=%lx, secheader str indx=%lx",elfHeader,elfHeader->e_shoff,elfHeader->e_shstrndx);
    for(int i=0;i<EI_NIDENT;i++)
        Log(trace,"%x ",elfHeader->e_ident[i]);
    Elf64_Phdr* phdrTable=(Elf64_Phdr*)(buff+elfHeader->e_phoff);

    xlen_t programbreak=0;
    for(int i=0;i<elfHeader->e_phnum;i++){
        const Elf64_Phdr &entry=phdrTable[i];
        if(entry.p_type!=PT_LOAD)continue;
        auto pages=vm::addr2pn(entry.p_vaddr+entry.p_memsz-1)-vm::addr2pn(entry.p_vaddr)+1;
        programbreak=klib::max(programbreak,entry.p_vaddr+entry.p_memsz);
        vm::PageNum ppn=(kGlobObjs->pageMgr->alloc(pages));
        Log(debug,"%x<=%x[%d pages@%x]",vm::addr2pn(entry.p_vaddr),ppn,pages,ld::elf::flags2perm(entry.p_flags));
        auto vmo=make_shared<vm::VMOContiguous>(ppn,pages);
        vmar.map(vm::PageMapping{vm::addr2pn(entry.p_vaddr),pages,0,vmo,ld::elf::flags2perm(entry.p_flags),vm::PageMapping::MappingType::file});
        memcpy((ptr_t)vm::pn2addr(ppn)+vm::addr2offset(entry.p_vaddr),buff+entry.p_offset,entry.p_filesz);
    }
    vmar.print();
    return eastl::make_tuple(elfHeader->e_entry,programbreak);
}
eastl::tuple<xlen_t,xlen_t> ld::loadElf(shared_ptr<fs::File> file,vm::VMAR &vmar){
    auto vmo=file->vmo();
    vm::VMOMapper mapper(vmo);
    auto buff=(uint8_t*)mapper.start();
    Elf64_Ehdr *elfHeader=(Elf64_Ehdr*)(buff);
    Log(info,"Elf header=%p, section header table=%lx, secheader str indx=%lx",elfHeader,elfHeader->e_shoff,elfHeader->e_shstrndx);
    for(int i=0;i<EI_NIDENT;i++)
        Log(trace,"%x ",elfHeader->e_ident[i]);
    Elf64_Phdr* phdrTable=(Elf64_Phdr*)(buff+elfHeader->e_phoff);

    xlen_t programbreak=0;
    for(int i=0;i<elfHeader->e_phnum;i++){
        const Elf64_Phdr &entry=phdrTable[i];
        if(entry.p_type!=PT_LOAD)continue;
        auto pages=vm::addr2pn(entry.p_vaddr+entry.p_memsz-1)-vm::addr2pn(entry.p_vaddr)+1;
        programbreak=klib::max(programbreak,entry.p_vaddr+entry.p_memsz);
        using mask=vm::PageTableEntry::fieldMasks;
        auto perm=ld::elf::flags2perm(entry.p_flags);
        if(!perm&mask::w)
            vmar.map(vm::PageMapping{vm::addr2pn(entry.p_vaddr),pages,vm::addr2pn(entry.p_offset),vmo,perm,vm::PageMapping::MappingType::file,vm::PageMapping::SharingType::shared});
        else {
            auto pager=make_shared<vm::SwapPager>(eastl::dynamic_pointer_cast<vm::VMOPaged>(vmo)->pager);
            pager->backingoff=vm::addr2pn(entry.p_offset);
            auto shadow=make_shared<vm::VMOPaged>(pages,pager);
            vmar.map(vm::PageMapping{vm::addr2pn(entry.p_vaddr),pages,0,shadow,perm,vm::PageMapping::MappingType::file,vm::PageMapping::SharingType::shared});
        }
        // Log(debug,"%x<=%x[%d pages@%x]",vm::addr2pn(entry.p_vaddr),ppn,pages,ld::elf::flags2perm(entry.p_flags));
    }
    vmar.print();
    return eastl::make_tuple(elfHeader->e_entry,programbreak);
}