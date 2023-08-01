#pragma once
#ifndef DISK_HH__
#define DISK_HH__

#include "virtio.hh"
#include "sd.hh"
#include "kernel.hh"
// #define moduleLevel debug
// #define QEMU 1
inline void disk_init() {
    #ifdef QEMU
    virtio_disk_init();
    #else 
    // SD::init();
    #endif
}

inline void disk_rw(bio::BlockBuf &b, int a_write) {
    #ifdef QEMU
    virtio_disk_rw(b, 0);
    #else 
    // if(a_write) { SD::write(b.key.secno, b.d, 512); }
    // else { SD::read(b.key.secno, b.d, 512); }
    Log(debug,"rw secno=%d buf=%x",b.key.secno,b.d);
    auto mempos=kInfo.segments.ramdisk.first+(b.key.secno*512);
    if(mempos>kInfo.segments.ramdisk.second) panic("mempos out of bound");
    if(a_write) memmove((ptr_t)mempos,b.d,512);
    else memmove(b.d,(ptr_t)mempos,512);
    #endif
}

inline void disk_intr() {
    #ifdef QEMU
    virtio_disk_intr();
    #else 
    // dmac_intr(DMAC_CHANNEL0);  // @todo: 中断？
    #endif
}

#endif