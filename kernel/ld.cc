#include "../include/ld.hh"
#include "../include/kernel.hh"
#include "vm/vmo.hh"
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
        programbreak=klib::max(programbreak,entry.p_vaddr+pages);
        vm::PageNum ppn=(kGlobObjs->pageMgr->alloc(pages));
        Log(debug,"%x<=%x[%d pages@%x]",vm::addr2pn(entry.p_vaddr),ppn,pages,ld::elf::flags2perm(entry.p_flags));
        auto vmo=make_shared<vm::VMOContiguous>(ppn,pages);
        vmar.map(vm::PageMapping{vm::addr2pn(entry.p_vaddr),pages,0,vmo,ld::elf::flags2perm(entry.p_flags),vm::PageMapping::MappingType::file});
        memcpy((ptr_t)vm::pn2addr(ppn),buff+entry.p_offset,entry.p_filesz);
    }
    vmar.print();
    return eastl::make_tuple(elfHeader->e_entry,programbreak);
}
xlen_t ld::loadElf(shared_ptr<fs::File> file,vm::VMAR &vmar){
    auto vmo=file->vmo();
    vm::VMOMapper mapper(vmo);
    auto buff=(uint8_t*)mapper.start();
    Elf64_Ehdr *elfHeader=(Elf64_Ehdr*)(buff);
    Log(info,"Elf header=%p, section header table=%lx, secheader str indx=%lx",elfHeader,elfHeader->e_shoff,elfHeader->e_shstrndx);
    for(int i=0;i<EI_NIDENT;i++)
        Log(trace,"%x ",elfHeader->e_ident[i]);
    Elf64_Phdr* phdrTable=(Elf64_Phdr*)(buff+elfHeader->e_phoff);

    for(int i=0;i<elfHeader->e_phnum;i++){
        const Elf64_Phdr &entry=phdrTable[i];
        if(entry.p_type!=PT_LOAD)continue;
        auto pages=vm::addr2pn(entry.p_vaddr+entry.p_memsz-1)-vm::addr2pn(entry.p_vaddr)+1;
        // Log(debug,"%x<=%x[%d pages@%x]",vm::addr2pn(entry.p_vaddr),ppn,pages,ld::elf::flags2perm(entry.p_flags));
        vmar.map(vm::PageMapping{vm::addr2pn(entry.p_vaddr),pages,vm::addr2pn(entry.p_offset),vmo,ld::elf::flags2perm(entry.p_flags),vm::PageMapping::MappingType::file,vm::PageMapping::SharingType::shared});
    }
    vmar.print();
    return elfHeader->e_entry;
}