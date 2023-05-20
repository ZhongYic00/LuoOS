#include "../include/ld.hh"
#include "../include/kernel.hh"
#include <elf.h>

#define Log printf

xlen_t ld::loadElf(const uint8_t *buff,vm::VMAR &vmar){
    Elf64_Ehdr *elfHeader=(Elf64_Ehdr*)(buff);
    Log("Elf header=%p, section header table=%lx, secheader str indx=%lx",elfHeader,elfHeader->e_shoff,elfHeader->e_shstrndx);
    for(int i=0;i<EI_NIDENT;i++)
        printf("%x ",elfHeader->e_ident[i]);
    putchar('\n');
    Elf64_Phdr* phdrTable=(Elf64_Phdr*)(buff+elfHeader->e_phoff);

    for(int i=0;i<elfHeader->e_phnum;i++){
        const Elf64_Phdr &entry=phdrTable[i];
        if(entry.p_type!=PT_LOAD)continue;
        int pages=vm::bytes2pages(entry.p_memsz);
        vm::PageNum ppn=(kGlobObjs.pageMgr->alloc(pages));
        printf("%x<=%x[%d pages@%x]",vm::addr2pn(entry.p_vaddr),ppn,pages,ld::elf::flags2perm(entry.p_flags));
        vmar.map(vm::addr2pn(entry.p_vaddr),ppn,pages,ld::elf::flags2perm(entry.p_flags));
        memcpy((ptr_t)vm::pn2addr(ppn),buff+entry.p_offset,entry.p_filesz);
    }
    return elfHeader->e_entry;
}