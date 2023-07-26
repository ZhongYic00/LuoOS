// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

// #include "include/spinlock.h"
// #include "include/sleeplock.h"
// #include "include/sdcard.h"
// #include "include/printf.h"
// #include "disk.h"
#include "virtio.hh"
#include "alloc.hh"
#include "kernel.hh"
#include "klib.h"
#include <EASTL/bonus/lru_cache.h>
#include "bio.hh"

// #define moduleLevel debug
bio::BCacheMgr bcache;

void test(){
    // auto ref=bcache[{1,1}];
    // (*ref)[10]=100;
    // int read=(*ref)[10];
}

struct {
    // struct spinlock lock;
    semaphore::Semaphore sema;
} freecache;

void bio::init(void)
{
    new ((void*)&bcache) bio::BCacheMgr();
    /// @todo use sema to control concurrency
    new ((ptr_t)&freecache.sema) semaphore::Semaphore(bio::BCacheMgr::defaultSize);
}

xlen_t bufsCreated,bufsDestructed;
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
namespace bio{
    void BlockBuf::reload(){
        /// @todo should use devmgr, <blockdev>(devmgr[dev]).read(sec)
        virtio_disk_rw(*this,0);
    }
    BlockBuf::BlockBuf(const BlockKey& key_):key(key_),d(reinterpret_cast<uint8_t*>(vm::pn2addr(kGlobObjs->pageMgr->alloc(1)))){
        ++bufsCreated;
        reload();
    }
    BlockBuf::~BlockBuf(){
        ++bufsDestructed;
        flush();
        // delete reinterpret_cast<AlignedBytes<>*>(d);
        kGlobObjs->pageMgr->free(vm::addr2pn((xlen_t)d),0);
    }
    void BlockBuf::flush(){
        Log(debug,"blockbuf flush");
        // virtio_disk_rw(*this,1);
        dirty=false;
    }
}