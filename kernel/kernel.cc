#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"
#include "vm.hh"
#include "alloc.hh"
#include "ld.hh"
#include "sched.hh"
#include "proc.hh"

extern char _kstack_end;
xlen_t kstack_end=(xlen_t)&_kstack_end;
kernel::KernelGlobalObjs kGlobObjs;
kernel::KernelHartObjs kHartObjs;
kernel::KernelInfo kInfo={
    .segments={
        .dev=(vm::segment_t){0x0,0x40000000}
    }
};
using vm::pgtbl_t,vm::PageTable;
__attribute__((section("pagetable")))
vm::PageTableEntry kernelPageTableRoot[vm::pageEntriesPerPage];
kernel::KernelObjectsBuf kObjsBuf;
alloc::PageMgr *kernelPmgr=(alloc::PageMgr*)kObjsBuf.kPageMgrBuf;
alloc::HeapMgr *kHeapMgr=(alloc::HeapMgr*)kObjsBuf.kHeapMgrBuf;
uint8_t pool[32*vm::pageSize];

PageTable *kernelPageTable=(vm::PageTable*)kObjsBuf.kPageTableBuf;

static void timerInit(){
    int hart=sbi::readHartId();
    printf("hart=%d\n",hart);
    mmio<xlen_t>(platform::clint::mtimecmpOf(hart))=mmio<xlen_t>(platform::clint::mtime)+kernel::timerInterval;
    csrSet(sie,BIT(csr::mie::stie));
}

__attribute__((nacked, section("stub")))
static void stub(){
    csrWrite(satp,kernelPageTableRoot);
    ExecInst(sfence.vma);
    ExecInst(j strapwrapper);
}

// __attribute__((section("init")))
void kernel::createKernelMapping(PageTable &pageTable){
    using perm=vm::PageTableEntry::fieldMasks;
    { auto &seg=kInfo.segments.text;pageTable.createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::x);}
    DBG(kernelPageTable->print();)
    { auto &seg=kInfo.segments.rodata;pageTable.createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r);}
    { auto &seg=kInfo.segments.kstack;pageTable.createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::w);}
    { auto &seg=kInfo.segments.data;pageTable.createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::w);}
    { auto &seg=kInfo.segments.bss;pageTable.createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::w);}
}
static void memInit(){
    csr::satp satp;
    satp.mode=8;
    satp.asid=0;
    satp.ppn=vm::addr2pn((xlen_t)kernelPageTableRoot);
    new ((void*)&kObjsBuf.kPageTableBuf) PageTable(kernelPageTableRoot);
    new ((void*)&kObjsBuf.kHeapMgrBuf) alloc::HeapMgr(pool,sizeof(pool));
    new ((void*)&kObjsBuf.kPageMgrBuf) alloc::PageMgr(vm::addr2pn(kInfo.segments.frames.first),vm::addr2pn(kInfo.segments.frames.second));
    kHeapMgr=new alloc::HeapMgrGrowable(*kHeapMgr,*kernelPmgr);
    // kernelPageTable->createMapping(0,vm::addr2pn(0x00000000),3*0x40000,0xcf); // naive direct mapping
    using perm=vm::PageTableEntry::fieldMasks;
    kernel::createKernelMapping(*kernelPageTable);
    { auto &seg=kInfo.segments.dev; kernelPageTable->createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first),perm::v|perm::r|perm::w);}
    { auto &seg=kInfo.segments.frames; kernelPageTable->createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first),perm::v|perm::r|perm::w);}
    { auto seg=(vm::segment_t){0x84000000,0x84200000}; kernelPageTable->createMapping(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first),perm::v|perm::r|perm::w);}

    kernelPageTable->print();
    csrWrite(satp,kGlobObjs.ksatp=satp.value());
    ExecInst(sfence.vma);

}
static void infoInit(){
    extern char _text_start,_text_end;
    extern char _rodata_start,_rodata_end;
    extern char _data_start,_data_end;
    extern char _bss_start,_bss_end;
    extern char _frames_start,_frames_end;
    extern char _kstack_start,_kstack_end;
    kInfo.segments.text={(xlen_t)&_text_start,(xlen_t)&_text_end};
    kInfo.segments.rodata={(xlen_t)&_rodata_start,(xlen_t)&_rodata_end};
    kInfo.segments.data={(xlen_t)&_data_start,(xlen_t)&_data_end};
    kInfo.segments.kstack={(xlen_t)&_kstack_start,(xlen_t)&_kstack_end};
    kInfo.segments.bss={(xlen_t)&_bss_start,(xlen_t)&_bss_end};
    kInfo.segments.frames={(xlen_t)&_frames_start,(xlen_t)&_frames_end};
    for(int i=0;i<sizeof(kInfo.segments)/sizeof(kInfo.segments.bss);i++){
        printf("{0x%lx 0x%lx}\n",*(((vm::segment_t*)&kInfo.segments)+i));
    }
}

extern void schedule();
extern void program0();
extern "C" void strapwrapper();
extern void _strapexit();
extern "C" //__attribute__((section("init")))
void start_kernel(){
    register ptr_t sp asm("sp");
    // auto &ctx=kHartObjs.curtask->ctx; //TODO fixbug
    // ctx.kstack=sp;
    puts=IO::_blockingputs;
    puts("\n\n>>>Hello RVOS<<<\n\n");
    infoInit();
    memInit();
    // csrWrite(sscratch,ctx.gpr);
    csrWrite(stvec,strapwrapper);
    csrSet(sstatus,BIT(csr::mstatus::sum));
    csrSet(sstatus,BIT(csr::mstatus::sie));
    csrSet(sie,BIT(csr::mie::ssie));
    // halt();
    // while(true);
    for(int i=0;i<10;i++)
        printf("%d:Hello RVOS!\n",i);
    extern char _uimg_start;
    auto uproc=proc::createProcess();
    uproc->defaultTask()->ctx.pc=ld::loadElf((uint8_t*)((xlen_t)&_uimg_start),uproc->pagetable);
    kGlobObjs.scheduler.add(uproc->defaultTask());
    uproc=proc::createProcess();
    uproc->defaultTask()->ctx.pc=ld::loadElf((uint8_t*)((xlen_t)&_uimg_start),uproc->pagetable);
    kGlobObjs.scheduler.add(uproc->defaultTask());
    timerInit();
    csrClear(sstatus,1l<<csr::mstatus::spp);
    csrSet(sstatus,BIT(csr::mstatus::spie));
    schedule();
    volatile register ptr_t t6 asm("t6")=kHartObjs.curtask->ctx.gpr;
    _strapexit();
    // halt();
}