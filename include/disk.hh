#pragma once
#ifndef DISK_HH__
#define DISK_HH__

#include "virtio.hh"
#include "sd.hh"
#define QEMU 1
inline void disk_init() {
    #ifdef QEMU
    virtio_disk_init();
    #else 
    SD::init();
    #endif
}

inline void disk_rw(bio::BlockBuf &b, int a_write) {
    #ifdef QEMU
    virtio_disk_rw(b, 0);
    #else 
    if(a_write) { SD::write(b.key.secno, b.d, BlockBuf::blockSize); }
    else { SD::read(b.key.secno, b.d, BlockBuf::blockSize); }
    #endif
}

inline void disk_intr() {
    #ifdef QEMU
    virtio_disk_intr();
    #else 
    // dmac_intr(DMAC_CHANNEL0);
    #endif
}

#endif