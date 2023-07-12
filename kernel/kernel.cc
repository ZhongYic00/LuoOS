#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"
#include "vm.hh"
#include "alloc.hh"
#include "ld.hh"
#include "sched.hh"
#include "proc.hh"
#include "virtio.hh"
#include "fs.hh"
#include "fs/ramfs.hh"

#define moduleLevel LogLevel::info

extern char _kstack_end;
xlen_t kstack_end=(xlen_t)&_kstack_end;
kernel::KernelHartObjs kHartObjs[8];
extern char _kstack_start,_kstack_end;
kernel::KernelInfo kInfo={
    .segments={
        .dev=(vm::segment_t){0x0,0x40000000},
        .kstack=vm::segment_t{(xlen_t)&_kstack_start,(xlen_t)&_kstack_end}
    }
};
using vm::pgtbl_t,vm::PageTable;
__attribute__((section("pagetable")))
vm::PageTableEntry kernelPageTableRoot[vm::pageEntriesPerPage];
kernel::KernelObjectsBuf kObjsBuf;
kernel::KernelGlobalObjs *kGlobObjs=reinterpret_cast<kernel::KernelGlobalObjs*>(&kObjsBuf.kGlobObjsBuf);
uint8_t pool[32*vm::pageSize];

void nextTimeout(){
    xlen_t time;
    csrRead(time,time);
    sbi_set_timer(time+kernel::timerInterval);
}
static void timerInit(){
    kHartObj().g_ticks = 0;
    csrSet(sie,BIT(csr::mie::stie));
    nextTimeout();
}

void uartInitTest(){
    using namespace platform::uart0::nonblocking;
    for(int i=1024;i;i--)putc('_');
    putc('\n');
    // 0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg0123456789abcdefg
}
void uartInit(){
    using namespace platform::uart0;
    auto &ier=mmio<volatile uint8_t>(reg(IER));
    ier=0x00;
    puts("Hello Uart\n");
    auto &lcr=mmio<volatile uint8_t>(reg(LCR));
    lcr=lcr|(1<<7);
    mmio<volatile uint8_t>(reg(DLL))=0x03;
    mmio<volatile uint8_t>(reg(DLM))=0x00;
    lcr=3;
    mmio<volatile uint8_t>(reg(FCR))=0x7|(0x3<<6);
    ier=0x01;
    // puts=IO::_nonblockingputs;
}
static void plicInit(){
    int hart=kernel::readHartId();
    using namespace platform::plic;

    uartInit();
    // mmio<word_t>(priorityOf(platform::uart0::irq))=1;
    // mmio<word_t>(enableOf(hart))=1<<platform::uart0::irq;

    mmio<word_t>(priorityOf(platform::virtio::blk::irq))=1;
    mmio<word_t>(enableOf(hart))|=1<<platform::virtio::blk::irq;

    mmio<word_t>(thresholdOf(hart))=0;
    
    uartInitTest();
}

// __attribute__((section("init")))
void kernel::createKernelMapping( vm::VMAR &vmar){
    using perm=vm::PageTableEntry::fieldMasks;
    { auto &seg=kInfo.segments.text;vmar.map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::x,vm::VMO::CloneType::shared);}
    DBG(vmar.print();)
    { auto &seg=kInfo.segments.rodata;vmar.map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r,vm::VMO::CloneType::shared);}
    { auto &seg=kInfo.segments.kstack;vmar.map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::w,vm::VMO::CloneType::shared);}
    { auto &seg=kInfo.segments.data;vmar.map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::w,vm::VMO::CloneType::shared);}
    { auto &seg=kInfo.segments.bss;vmar.map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,perm::v|perm::r|perm::w,vm::VMO::CloneType::shared);}
}
kernel::KernelGlobalObjs::KernelGlobalObjs():
    heapMgr(pool,sizeof(pool),pageMgr),
    pageMgr(vm::addr2pn(kInfo.segments.frames.first),vm::addr2pn(kInfo.segments.frames.second)),
    vmar((std::initializer_list<vm::PageMapping>){},kernelPageTableRoot)
    {}
static void memInit(){
    Log(info,"initializing mem...");
    csr::satp satp;
    satp.mode=8;
    satp.asid=0;
    satp.ppn=vm::addr2pn((xlen_t)kernelPageTableRoot);
    auto kGlobObjsInternal=new ((void*)kObjsBuf.kGlobObjsBuf) kernel::KernelGlobalObjs();
    // new ((void*)&kGlobObjs) kernel::KernelGlobalObjsRef(*kGlobObjsInternal);
    // kGlobObjs.pageMgr=(alloc::PageMgr*)kObjsBuf.kPageMgrBuf;
    // kGlobObjs.heapMgr=(alloc::HeapMgr*)kObjsBuf.kHeapMgrBuf;
    // kGlobObjs.vmar=(vm::VMAR*)kObjsBuf.kVMARBuf;
    // new ((void*)&kObjsBuf.kHeapMgrBuf) alloc::HeapMgr(pool,sizeof(pool));
    // new ((void*)&kObjsBuf.kPageMgrBuf) alloc::PageMgr(vm::addr2pn(kInfo.segments.frames.first),vm::addr2pn(kInfo.segments.frames.second));
    // kGlobObjs.heapMgr=new alloc::HeapMgrGrowable(*kGlobObjs.heapMgr,*kGlobObjs.pageMgr);
    // new ((void*)&kObjsBuf.kVMARBuf) vm::VMAR({},kernelPageTableRoot);
    // kGlobObjs.vmar->map(0,vm::addr2pn(0x00000000),3*0x40000,0xcf); // naive direct mapping
    using perm=vm::PageTableEntry::fieldMasks;
    kernel::createKernelMapping(*(kGlobObjs->vmar.get()));
    { auto &seg=kInfo.segments.dev; kGlobObjs->vmar->map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first),perm::v|perm::r|perm::w);}
    { auto &seg=kInfo.segments.frames; kGlobObjs->vmar->map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first),perm::v|perm::r|perm::w);}
    { auto seg=(vm::segment_t){0x84000000,0x84200000}; kGlobObjs->vmar->map(vm::addr2pn(seg.first),vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first),perm::v|perm::r|perm::w);}

    // kernel::KernelGlobalObjsRef ref(kGlobObjs);
    // ref->ksatp=1;
    kGlobObjs->vmar->print();
    Log(info,"is about to enable kernel vm");
    csrWrite(satp,kGlobObjs->ksatp=satp.value());
    ExecInst(sfence.vma);
    Log(info,"kernel vm enabled, memInit success");
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
        Log(info,"{0x%lx 0x%lx}",*(((vm::segment_t*)&kInfo.segments)+i));
    }
}
static void rootfsInit(){
    // auto rootfs=new ramfs::FileSystem();
    // auto root=rootfs->getRoot();
}
void idle(){
    while(true){
        Log(debug,"kidle...");
        /// @bug after interrupt, sepc not +4, always return to instr wfi ?
        ExecInst(wfi);
    }
}
std::atomic<uint32_t> started;
mutex::spinlock spin;
extern void schedule();
extern void program0();
extern void strapwrapper();
extern void _strapexit();

FORCEDINLINE
xlen_t irqStackOf(int hart){return kInfo.segments.kstack.first+hart*0x1000+0x1000;}

void init(int hartid){
    puts=IO::_sbiputs;
    puts("\n\n>>>Hello LuoOS<<<\n\n");
    sbi_init();
    int prevStarted=started.fetch_add(1);
    bool isinit=false;
    Log(info,"Hart %d online!",hartid);
    
    if(!prevStarted){   // is first hart
        // @todo needs plic and uart init?
        csrWrite(stvec,strapwrapper);
        puts=IO::_blockingputs;
        isinit=true;

        infoInit();
        memInit();
        syscall::init();
        // csrWrite(sscratch,ctx.gpr);
        csrSet(sstatus,BIT(csr::mstatus::sum));
        // csrSet(sstatus,BIT(csr::mstatus::sie));
        csrSet(sie,BIT(csr::mie::ssie)|BIT(csr::mie::seie));
        // halt();
        // while(true);
        for(int i=0;i<10;i++)
            printf("%d:Hello LuoOS!\n",i);
        extern char _uimg_start;
        auto uproc=proc::createProcess();
        uproc->name="uprog00";
        uproc->defaultTask()->ctx.pc=ld::loadElf((uint8_t*)((xlen_t)&_uimg_start),uproc->vmar);
        // plicInit();
        // csrClear(sstatus,1l<<csr::mstatus::spp);
        // csrSet(sstatus,BIT(csr::mstatus::spie));
        virtio_disk_init();
        Log(info,"virtio disk init over");
        bio::init();
        Log(info,"binit over");
        std::atomic_thread_fence(std::memory_order_release);
        sbi_hsm_hart_start(hartid^1,(xlen_t)0x80200000,0);
        while(started<2){
            auto rt=sbi_hsm_hart_get_status(1);
        }
        xlen_t mask=0x1;
        sbi_remote_sfence_vma((cpumask*)&mask,0x0,0x84000000);
        // for(volatile int i=10000000;i;i--);
    } else {
        csrWrite(stvec,strapwrapper);
        csrSet(sstatus,BIT(csr::mstatus::sum));
        // csrSet(sstatus,BIT(csr::mstatus::sie));
        csrSet(sie,BIT(csr::mie::ssie)|BIT(csr::mie::seie));
    }
    {// test lock
        using namespace mutex;
        lock_guard<spinlock<>> guard(spin);
        printf("lock acquired! %d\n",hartid);
    }
    assert(hartid==1||hartid==0);
    if(true){
        timerInit();
        plicInit();
        std::atomic_thread_fence(std::memory_order_acquire);
        auto kidle=proc::createKProcess(sched::maxPrior);
        kidle->defaultTask()->kctx.ra()=(xlen_t)idle;
        schedule();
        Log(info,"first schedule on hart%d",kernel::readHartId());
        kLogger.outputLevel=warning;
        _strapexit();
    }
    halt();
}

extern "C" __attribute__((naked))
void start_kernel(int hartid){
    register int tp asm("tp")=hartid;
    register xlen_t sp asm("sp");
    sp=irqStackOf(hartid);
    init(hartid);
}