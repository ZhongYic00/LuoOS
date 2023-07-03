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
#include "buf.h"
// #include "include/sdcard.h"
// #include "include/printf.h"
// #include "disk.h"
#include "virtio.h"
#include "alloc.hh"
// #include "include/proc.h"
#include "klib.h"
#include <EASTL/bonus/lru_cache.h>
#include "bio.hh"
bio::BCacheMgr bcache;

void test(){
    auto ref=bcache[{1,1}];
    (*ref)[10]=100;
    int read=(*ref)[10];
}

struct buf blockBufs[NBUF];

struct {
  // struct spinlock lock;
  semaphore::Semaphore sema;
} freecache;

void
binit(void)
{
  new ((void*)&bcache) bio::BCacheMgr();
  test();
  new ((ptr_t)&freecache.sema) semaphore::Semaphore(bio::BCacheMgr::defaultSize);
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint sectorno) {
  Log(debug,"bget dev=%d, secno=%d",dev,sectorno);
}


struct buf* 
bread(uint dev, uint sectorno) {
  struct buf *b;
  b = bget(dev, sectorno);

  if (!b->vaild) {
    virtio_disk_rw(b, 0);
    b->vaild = 1;
  }

  return b;
}

// Write b's contents to disk.  Must be locked.
void 
bwrite(struct buf *b) {
  virtio_disk_rw(b, 1);;
  b->vaild = 1;
}


void
brelse(struct buf *b)
{
  // acquire(&freecache.lock);
  b->busy = 0;

  // @todo 进程相关
  // wakeup sleeping processes because of b is busy
  // wakeup(b);
  b->sema.rel();
  freecache.sema.rel();
  // wakeup sleeping processes because of NOFREEBUF
  // wakeup(&freecache);
}

