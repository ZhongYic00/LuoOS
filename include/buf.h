#ifndef __BUF_H
#define __BUF_H

#define NBUF         30  // size of disk block cache
#define NBUFLIST     4
#define BSIZE 512
#include "common.h"
#include "lock.hh"

struct buf {
  uint8 busy;
  uint8 vaild;
  int disk;		// does disk "own" buf? 
  uint dev;
  uint sectorno;	// sector number 
  // struct sleeplock lock;
  semaphore::Semaphore sema;
  // uint refcnt;
  struct buf *freeprev;
  struct buf *freenext;
  struct buf *prev;
  struct buf *next;
  uchar data[BSIZE];
};

void binit(void);
struct buf* bread(uint, uint);
void brelse(struct buf*);
void bwrite(struct buf*);

#endif