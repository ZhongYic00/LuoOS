#ifndef __BUF_H
#define __BUF_H

#define BSIZE 512
// #include "sleeplock.h"
#include "types.h"

typedef word_t secno_t;

struct BlockBuf {
  // struct sleeplock lock;
  uint8_t data[BSIZE];
  template<typename T=uint32>
  inline T& operator[](off_t off){return reinterpret_cast<T*>(data)[off];}
  template<typename T=uint32>
  inline T& d(off_t off){return reinterpret_cast<T*>(data)[off];}
};
struct BlockRef{
  secno_t secno;
  BlockBuf &buf;
  template<typename T=uint32>
  inline T& operator[](off_t off){return reinterpret_cast<T*>(buf.data)[off];}
  template<typename T=uint32>
  inline T& d(off_t off){return reinterpret_cast<T*>(buf.data)[off];}
};

void binit(void);
BlockRef& bread(uint, uint);
void brelse(BlockRef&);
void bwrite(BlockRef&);

#endif