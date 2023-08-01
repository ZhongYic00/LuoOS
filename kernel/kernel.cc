#include "klib.hh"
#include "rvcsr.hh"
#include "sbi.hh"
#include "kernel.hh"
#include "vm.hh"
#include "alloc.hh"
#include "ld.hh"
#include "sched.hh"
#include "proc.hh"
// #include "virtio.hh"
#include "disk.hh"
#include "fs.hh"
#include "fs/ramfs.hh"
#include "vm/vmo.hh"

#define moduleLevel LogLevel::info

extern char _kstack_end;
xlen_t kstack_end=(xlen_t)&_kstack_end;
kernel::KernelHartObjs kHartObjs[8];
extern char _text_start,_text_end;
extern char _rodata_start,_rodata_end;
extern char _data_start,_data_end;
extern char _vdso_start,_vdso_end;
extern char _bss_start,_bss_end;
extern char _frames_start,_frames_end;
extern char _kstack_start,_kstack_end;
extern char _memdisk_start,_memdisk_end;
kernel::KernelInfo kInfo={
    .segments={
        .dev=(vm::segment_t){0x0,0x40000000},
        .text={(xlen_t)&_text_start,(xlen_t)&_text_end},
        .rodata={(xlen_t)&_rodata_start,(xlen_t)&_rodata_end},
        .data={(xlen_t)&_data_start,(xlen_t)&_data_end},
        .vdso={(xlen_t)&_vdso_start,(xlen_t)&_vdso_end},
        .kstack=vm::segment_t{(xlen_t)&_kstack_start,(xlen_t)&_kstack_end},
        .bss={(xlen_t)&_bss_start,(xlen_t)&_bss_end},
        .frames={(xlen_t)&_frames_start,(xlen_t)&_frames_end},
        .ramdisk={(xlen_t)&_memdisk_start,(xlen_t)&_memdisk_end}
        .mapper={vm::addr2pn((xlen_t)&_memdisk_end)+2,vm::addr2pn((xlen_t)&_memdisk_end)+2},
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
    Log(debug,"curtime=%ld,next=%ld",time,time+kernel::timerInterval);
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
    using namespace vm;
    using perm=PageTableEntry::fieldMasks;
    using mapping=PageMapping::MappingType;
    using sharing=PageMapping::SharingType;
    { auto &seg=kInfo.segments.text;vmar.map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,0,kInfo.vmos.text,perm::v2|perm::r|perm::w|perm::x,mapping::system,sharing::shared});}
    { auto &seg=kInfo.segments.rodata;vmar.map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,0,kInfo.vmos.rodata,perm::v2|perm::r|perm::w|perm::x,mapping::system,sharing::shared});}
    { auto &seg=kInfo.segments.kstack;vmar.map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,0,kInfo.vmos.kstack,perm::v2|perm::r|perm::w|perm::x,mapping::system,sharing::shared});}
    { auto &seg=kInfo.segments.data;vmar.map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,0,kInfo.vmos.data,perm::v2|perm::r|perm::w|perm::x,mapping::system,sharing::shared});}
    { auto &seg=kInfo.segments.bss;vmar.map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second)-vm::addr2pn(seg.first)+1,0,kInfo.vmos.bss,perm::v2|perm::r|perm::w|perm::x,mapping::system,sharing::shared});}
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
    memset(kernelPageTableRoot,0,sizeof(kernelPageTableRoot));
    auto kGlobObjsInternal=new ((void*)kObjsBuf.kGlobObjsBuf) kernel::KernelGlobalObjs();
    using namespace vm;
    using perm=PageTableEntry::fieldMasks;
    using mapping=PageMapping::MappingType;
    using sharing=PageMapping::SharingType;
    using namespace vm;
    { auto &seg=kInfo.segments.text;auto st=addr2pn(seg.first),ed=addr2pn(seg.second),pages=ed-st+1;kInfo.vmos.text=make_shared<VMOContiguous>(st,pages);}
    { auto &seg=kInfo.segments.rodata;auto st=addr2pn(seg.first),ed=addr2pn(seg.second),pages=ed-st+1;kInfo.vmos.rodata=make_shared<VMOContiguous>(st,pages);}
    { auto &seg=kInfo.segments.kstack;auto st=addr2pn(seg.first),ed=addr2pn(seg.second),pages=ed-st+1;kInfo.vmos.kstack=make_shared<VMOContiguous>(st,pages);}
    { auto &seg=kInfo.segments.data;auto st=addr2pn(seg.first),ed=addr2pn(seg.second),pages=ed-st+1;kInfo.vmos.data=make_shared<VMOContiguous>(st,pages);}
    { auto &seg=kInfo.segments.vdso;auto st=addr2pn(seg.first),ed=addr2pn(seg.second),pages=ed-st+1;kInfo.vmos.vdso=make_shared<VMOContiguous>(st,pages);}
    { auto &seg=kInfo.segments.bss;auto st=addr2pn(seg.first),ed=addr2pn(seg.second),pages=ed-st+1;kInfo.vmos.bss=make_shared<VMOContiguous>(st,pages);}
    kernel::createKernelMapping(*(kGlobObjs->vmar.get()));
    { auto &seg=kInfo.segments.dev;
        kInfo.vmos.dev=make_shared<VMOContiguous>(addr2pn(seg.first),addr2pn(seg.second-seg.first+1));
        kGlobObjs->vmar->map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first+1),0,kInfo.vmos.dev,perm::v2|perm::r|perm::w, mapping::anon,sharing::privt });}
    { auto &seg=kInfo.segments.frames;
        kInfo.vmos.frames=make_shared<VMOContiguous>(addr2pn(seg.first),addr2pn(seg.second-seg.first+1));
        kGlobObjs->vmar->map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first+1),0,kInfo.vmos.frames,perm::v2|perm::r|perm::w,mapping::anon,sharing::privt });}
    { auto &seg=kInfo.segments.ramdisk;
        kInfo.vmos.ramdisk=make_shared<VMOContiguous>(addr2pn(seg.first),addr2pn(seg.second-seg.first+1));
        kGlobObjs->vmar->map(PageMapping{ vm::addr2pn(seg.first),vm::addr2pn(seg.second-seg.first+1),0,kInfo.vmos.ramdisk,perm::v2|perm::r|perm::w,mapping::anon,sharing::privt });}

    kGlobObjs->vmar->print();
    Log(info,"is about to enable kernel vm");
    auto ksatp=kGlobObjs->ksatp=satp.value();
    Log(info,"ksatp=%lx",ksatp);
    // ExecInst(sfence.vma);
    csrWrite(satp,ksatp);
    ExecInst(sfence.vma);
    Log(info,"kernel vm enabled, memInit success");
}
static void infoInit(){
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
        Log(info,"kidle...");
        /// @bug after interrupt, sepc not +4, always return to instr wfi ?
        // ExecInst(wfi);
        while(true);
    }
}
std::atomic<uint32_t> started;
mutex::spinlock spin;
extern void schedule();
extern void program0();
extern void ktrapwrapper();
extern void _strapexit();

FORCEDINLINE
xlen_t irqStackOf(int hart){return kInfo.segments.kstack.first+hart*0x1000+0x1000;}
namespace fs{extern list<shared_ptr<vm::VMO>> vmolru;}
void init(int hartid){
    puts=IO::_sbiputs;
    puts("\n\n>>>Hello LuoOS<<<\n\n");
    int prevStarted=started.fetch_add(1);
    bool isinit=false;
    Log(info,"Hart %d online!",hartid);
    
    if(!prevStarted){   // is first hart
        // @todo needs plic and uart init?
        puts(
    "__          ___    _ _    _        _                  ____   _____ \n"\
    "\\ \\        / / |  | | |  | |      | |                / __ \\ / ____|\n"\
    " \\ \\  /\\  / /| |__| | |  | |______| |    _   _  ___ | |  | | (___  \n"\
    "  \\ \\/  \\/ / |  __  | |  | |______| |   | | | |/ _ \\| |  | |\\___ \\ \n"\
    "   \\  /\\  /  | |  | | |__| |      | |___| |_| | (_) | |__| |____) |\n"\
    "    \\/  \\/   |_|  |_|\\____/       |______\\__,_|\\___/ \\____/|_____/ \n"\
    "                                                                   \n"\
    "                                                                   \n"\
    );
        sbi_init();
        csrWrite(sstatus,0);
        csrWrite(stvec,ktrapwrapper);
        csrSet(sstatus,BIT(csr::mstatus::sum)|BIT(csr::mstatus::mxr)|BIT(csr::mstatus::fs));
        Log(info,"stvec set");
        // puts=IO::_blockingputs;
        isinit=true;

        infoInit();
        xlen_t mstatus,sstatus;
        // csrRead(mstatus,mstatus);
        csrRead(sstatus,sstatus);
        Log(info,"mstatus=%lx, sstatus=%lx",mstatus,sstatus);
        memInit();
        Log(info,"memInit ok, init syscall");
        syscall::init();
        Log(info,"syscall init ok,setting sstatus");
        // csrWrite(sscratch,ctx.gpr);
        // csrSet(sstatus,BIT(csr::mstatus::sie));
        csrSet(sie,BIT(csr::mie::ssie)|BIT(csr::mie::seie));
        // halt();
        // while(true);
        for(int i=0;i<10;i++)
            printf("%d:Hello LuoOS!\n",i);
        extern char _uimg_start;
        auto uproc=proc::createProcess();
        uproc->name="uprog00";
        auto [pc,brk]=ld::loadElf((uint8_t*)((xlen_t)&_uimg_start),uproc->vmar);
        uproc->defaultTask()->ctx.pc=pc;
        uproc->heapTop=brk;
        // plicInit();
        // csrClear(sstatus,1l<<csr::mstatus::spp);
        // csrSet(sstatus,BIT(csr::mstatus::spie));
        disk_init();
        Log(info,"virtio disk init over");
        bio::init();
        Log(info,"binit over");
        signal::sigInit();
        std::atomic_thread_fence(std::memory_order_release);
        // sbi_hsm_hart_start((hartid-1)^1+1,(xlen_t)0x80200000,0);
        // while(started<2){
        //     auto rt=sbi_hsm_hart_get_status(1);
        // }
        xlen_t mask=0x1;
        // sbi_remote_sfence_vma((cpumask*)&mask,0x0,0x84000000);
        // for(volatile int i=10000000;i;i--);
    } else {
        halt();
        // csrWrite(stvec,strapwrapper);
        // csrSet(sstatus,BIT(csr::mstatus::sum));
        // // csrSet(sstatus,BIT(csr::mstatus::sie));
        // csrSet(sie,BIT(csr::mie::ssie)|BIT(csr::mie::seie));
    }
    {// test lock
        using namespace mutex;
        lock_guard<spinlock<>> guard(spin);
        printf("lock acquired! %d\n",hartid);
    }
    
    new (&fs::vmolru) list<shared_ptr<vm::VMO>>();
    // assert(hartid==1||hartid==0);
    if(isinit){
        Log(info,"init timer");
        timerInit();
        Log(info,"init plic");
        plicInit();
        Log(info,"init over, create kidle");
        std::atomic_thread_fence(std::memory_order_acquire);
        auto kidle=proc::createKProcess(sched::maxPrior);
        kidle->defaultTask()->kctx.pc=(xlen_t)idle;
        kidle->defaultTask()->kctxs.push(kidle->defaultTask()->kctx);
        schedule();
        Log(info,"first schedule on hart%d",kernel::readHartId());
        kLogger.outputLevel=warning;
        enableLevel=warning;
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