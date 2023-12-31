#ifndef __PLATFORM_H__
#define __PLATFORM_H__
#include "common.h"
/*
 * QEMU RISC-V Virt machine with 16550a UART and VirtIO MMIO
 */

/* 
 * maximum number of CPUs
 * see https://github.com/qemu/qemu/blob/master/include/hw/riscv/virt.h
 * #define VIRT_CPUS_MAX 8
 */
#define MAXNUM_CPU 8

template<typename T>
FORCEDINLINE T& mmio(xlen_t addr){
    return reinterpret_cast<T&>(*reinterpret_cast<T*>(addr));
}
namespace platform{
    namespace uart0
    {
        constexpr auto irq=10;
        constexpr auto base=0x10000000l;
        enum regs{
            RHR=0,	// Receive Holding Register (read mode)
            THR=0,	// Transmit Holding Register (write mode)
            DLL=0,	// LSB of Divisor Latch (write mode)
            IER=1,	// Interrupt Enable Register (write mode)
            DLM=1,	// MSB of Divisor Latch (write mode)
            FCR=2,	// FIFO Control Register (write mode)
            ISR=2,	// Interrupt Status Register (read mode)
            LCR=3,	// Line Control Register
            MCR=4,	// Modem Control Register
            LSR=5,	// Line Status Register
            MSR=6,	// Modem Status Register
            SPR=7,	// ScratchPad Register
        };
        constexpr xlen_t reg(regs r){return base+r;}
        struct lsr
        {
            uint8_t rxnemp:1;
            uint8_t unused:4;
            uint8_t txidle:1;
            uint8_t unused1:2;
        };
        namespace blocking
        {    
            inline void putc(char c) {
                while(!mmio<lsr>(reg(LSR)).txidle);
                mmio<char>(reg(THR))=c;
            }
            inline char getc() {
                while(mmio<lsr>(reg(LSR)).rxnemp);
                return mmio<char>(reg(RHR));
            }
        } // namespace blocking
        namespace nonblocking
        {
            inline bool putc(char c){
                if(!mmio<lsr>(reg(LSR)).txidle)return false;
                mmio<char>(reg(THR))=c;
                return true;
            }
            inline char getc() {
                if(mmio<lsr>(reg(LSR)).rxnemp)return false;
                return mmio<char>(reg(RHR));
            }
        } // namespace nonblocking
        
    } // namespace uart0
    namespace plic
    {
        constexpr auto base=0x0c000000l;
        constexpr auto enable=base+0x2000,
        threshold=base+0x200000,
        claim=base+0x200004;
        namespace s{
            constexpr int contextOf(int hart) {return hart*2+1;}
        }
        namespace m{
            constexpr int contextOf(int hart) {return hart*2;}
        }
        using namespace s;
        constexpr auto priority=base,
            pending=base+0x1000;
        constexpr xlen_t priorityOf(int id) {return priority+id*4;}
        constexpr xlen_t enableOf(int hart) { return enable+contextOf(hart)*0x80; }
        constexpr xlen_t thresholdOf(int hart) { return threshold+contextOf(hart)*0x1000; }
        constexpr xlen_t claimOf(int hart) { return claim+contextOf(hart)*0x1000; }
    } // namespace plic
    namespace clint
    {
        constexpr auto base=0x2000000l,
            mtime=base+0xbff8,
            mtimecmp=base+0x4000
            ;
        constexpr xlen_t mtimecmpOf(int hart) { return mtimecmp+hart*0x8; }
    } // namespace clint
    
    namespace ram
    {
        constexpr auto start=0x80000000l,
            size=128l*0x100000l,
            end=start+size;
    } // namespace ram
    namespace virtio
    {
        namespace blk
        {
            constexpr auto irq=1;
            constexpr xlen_t base=0x10001000l;
            struct MMIOInterface{
                struct Config{
                    word_t magic,version,devId,venderId;
                    xlen_t devFeatures;
                    xlen_t:64;
                    xlen_t driverFeatures,guestPageSize;
                }config;
                static_assert(sizeof(Config)==0x30);
                struct Queue{
                    ///@brief select queue, write-only
                    word_t select;
                    ///@brief max size of current queue, read-only
                    const word_t maxSize;
                    ///@brief size of current queue, write-only
                    word_t size;
                    ///@brief used ring alignment, write-only
                    word_t align;
                    ///@brief physical page number for queue, read/write
                    word_t ppn;
                    ///@brief ready bit
                    word_t ready;
                    xlen_t:48;
                    ///@brief write-only
                    word_t notify;
                    xlen_t:48;
                }queue;
                static_assert(sizeof(Queue)==0x30);
                struct Interrupt{
                    word_t status;
                    word_t ack;
                    xlen_t:64;
                }intr;
                xlen_t status;
            };
        } // namespace blk
        
    } // namespace virtio
    
}

#endif /* __PLATFORM_H__ */