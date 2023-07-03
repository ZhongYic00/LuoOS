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
    ref[10]=100;
}

struct buf blockBufs[NBUF];

int listhash(uint dev, uint sectorno){
  return (dev+sectorno) & (NBUFLIST - 1);
}

struct {
  // struct spinlock lock;
  struct buf head;
} listcache[NBUFLIST];

struct {
  // struct spinlock lock;
  semaphore::Semaphore sema;
  struct buf head;
} freecache;

void
binit(void)
{
  new ((void*)&bcache) bio::BCacheMgr();
  test();
  struct buf *b;
  new ((ptr_t)&freecache.sema) semaphore::Semaphore(0);
  // initlock(&freecache.lock, "freecache");
  freecache.head.freeprev = &freecache.head;
  freecache.head.freenext = &freecache.head;
  for(int i = 0; i < NBUFLIST; i++){
    // initlock(&listcache[i].lock, "listcache");
    listcache[i].head.prev = &listcache[i].head;
    listcache[i].head.next = &listcache[i].head;
  }
  int i;
  for(b = blockBufs, i = 2; b < blockBufs+NBUF; b++, i++){
    new ((ptr_t)&b->sema) semaphore::Semaphore(1);
    b->busy = 0;
    b->dev = 0;
    b->sectorno = i;
    b->vaild = 0;
    b->freenext = freecache.head.freenext;
    b->freeprev = &freecache.head;
    freecache.sema.rel();
    freecache.head.freenext->freeprev = b;
    freecache.head.freenext = b;
    int n = listhash(b->dev, b->sectorno);
    b->next = listcache[n].head.next;
    b->prev = &listcache[n].head;
    listcache[n].head.next->prev = b;
    listcache[n].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint sectorno) {
  Log(debug,"bget dev=%d, secno=%d",dev,sectorno);
  while(1){
    struct buf *b;
    int n = listhash(dev, sectorno);
    Log(debug, "hashed bufno=%d",n);
    // acquire(&listcache[n].lock);
    for(b = listcache[n].head.next; b != &listcache[n].head; b = b->next){
      Log(trace, "n=%d, &listcache[n].head=0x%lx, b=0x%lx, b->next=0x%lx\n", n, &listcache[n].head, b, b->next);
      if(b->sectorno == sectorno && b->dev == dev){
        if(b->busy == 1){
          // release(&listcache[n].lock);
          // @todo 进程相关
          // sleep(b, NULL);
          // @bug 缺sleep导致死循环
          b->sema.req();
          goto next;
        }
        else{
          b->busy = 1;
          // release(&listcache[n].lock);
          // acquire(&freecache.lock);
          if(b->freenext){
            b->freenext->freeprev = b->freeprev;
            b->freeprev->freenext = b->freenext;
            b->freenext = NULL;
            b->freeprev = NULL;
          }
          // release(&freecache.lock);
          return b;
        }
      }
    }
    // release(&listcache[n].lock);
    // acquire(&freecache.lock);
    freecache.sema.req();
    for(b = freecache.head.freenext; b != &freecache.head; b = b->freenext){
      b->sema.req();
      b->busy = 1;
      b->vaild = 0;
      b->freenext->freeprev = b->freeprev;
      b->freeprev->freenext = b->freenext;
      b->freenext = NULL;
      b->freeprev = NULL;
      // release(&freecache.lock);
      n = listhash(b->dev, b->sectorno);
      // acquire(&listcache[n].lock);
      b->next->prev = b->prev;
      b->prev->next = b->next;
      // release(&listcache[n].lock);
      n = listhash(dev, sectorno);
      // acquire(&listcache[n].lock);
      b->next = listcache[n].head.next;
      b->prev = &listcache[n].head;
      listcache[n].head.next->prev = b;
      listcache[n].head.next = b;
      b->dev = dev;
      b->sectorno = sectorno;
      // release(&listcache[n].lock);
      return b;
    }
    // @todo 进程相关
    // sleep(&freecache, NULL);
    goto next;

    next:
      continue;
    
  }
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
  if(b->vaild){
    b->freenext = &freecache.head;
    b->freeprev = freecache.head.freeprev;
    freecache.head.freeprev->freenext = b;
    freecache.head.freeprev = b;
  }
  else{
    b->freenext = freecache.head.freenext;
    b->freeprev = &freecache.head;
    freecache.head.freenext->freeprev = b;
    freecache.head.freenext = b;
  }
  // release(&freecache.lock);

  // @todo 进程相关
  // wakeup sleeping processes because of b is busy
  // wakeup(b);
  b->sema.rel();
  freecache.sema.rel();
  // wakeup sleeping processes because of NOFREEBUF
  // wakeup(&freecache);
}

