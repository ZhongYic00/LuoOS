//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
// qemu presents a "legacy" virtio interface.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//


// #include "include/types.h"
// #include "include/riscv.h"
#include "kernel.hh"
#include "virtio.hh"
#include "proc.hh"
#include "vm.hh"
#include "klib.hh"
#include "bio.hh"
// #define moduleLevel debug
namespace syscall
{
  extern int sleep();
} // namespace syscall


list<proc::Task*> waiting;

static struct disk {
 // memory for virtio descriptors &c for queue 0.
 // this is a global instead of allocated because it must
 // be multiple contiguous pages, which kalloc()
 // doesn't support, and page aligned.
  /// @bug alignment???
  char pages[2*vm::pageSize];
  struct VRingDesc *desc;
  uint16 *avail;
  struct UsedArea *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    /// @todo 可能需要考虑并发同步，使用lock统一实现
    volatile int *ready;
    char status;
    proc::Task *waiting;
  } info[NUM];
  
  // struct spinlock vdisk_lock;
  
} __attribute__ ((aligned (vm::pageSize))) disk;

void
virtio_disk_init(void)
{
  uint32 status = 0;

  // initlock(&disk.vdisk_lock, "virtio_disk");
  auto &interface=mmio<volatile platform::virtio::blk::MMIOInterface>(platform::virtio::blk::base);
  if(interface.config.magic != 0x74726976 ||
     interface.config.version != 1 ||
     interface.config.devId != 2 ||
     interface.config.venderId != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  interface.status = status;

  status |= VIRTIO_CONFIG_S_DRIVER;
  interface.status = status;

  // negotiate features
  uint64 features = interface.config.devFeatures;
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  interface.config.driverFeatures = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  interface.status = status;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  interface.status = status;

  interface.config.guestPageSize = vm::pageSize;

  // initialize queue 0.
  interface.queue.select = 0;
  uint32 max = interface.queue.maxSize;
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");
  interface.queue.size = NUM;
  memset(disk.pages, 0, sizeof(disk.pages));
  interface.queue.ppn = vm::addr2pn((xlen_t)disk.pages);

  // desc = pages -- num * VRingDesc
  // avail = pages + 0x40 -- 2 * uint16, then num * uint16
  // used = pages + 4096 -- 2 * uint16, then num * vRingUsedElem

  disk.desc = (struct VRingDesc *) disk.pages;
  disk.avail = (uint16*)(((char*)disk.desc) + NUM*sizeof(struct VRingDesc));
  disk.used = (struct UsedArea *) (disk.pages + vm::pageSize);

  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
  Log(debug,"virtio_disk_init\n");
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("virtio_disk_intr 1");
  if(disk.free[i])
    panic("virtio_disk_intr 2");
  disk.desc[i].addr = 0;
  disk.free[i] = 1;
  while(!waiting.empty()){
    kGlobObjs->scheduler->wakeup(waiting.front());
    waiting.pop_front();
  }
}

// free a chain of descriptors.
static void
free_chain(int i)
{
  while(1){
    free_desc(i);
    if(disk.desc[i].flags & VRING_DESC_F_NEXT)
      i = disk.desc[i].next;
    else
      break;
  }
}

static int
alloc3_desc(int *idx)
{
  Log(trace,"alloc3_desc");
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

class IntGuard{
public:
    IntGuard(){
        csrClear(sstatus,BIT(csr::mstatus::sie));
    }
    ~IntGuard(){
        // csrSet(sstatus,BIT(csr::mstatus::sie));
    }
};

void
virtio_disk_rw(bio::BlockBuf &buf, int write)
{
  // IntGuard guard;
  uint64 sector = buf.key.secno;
  Log(debug,"diskrw sector=%ld",sector);
  for(bool success=false;!success;){
  // acquire(&disk.vdisk_lock);

  // the spec says that legacy block operations use three
  // descriptors: one for type/reserved/sector, one for
  // the data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    waiting.push_back(kHartObj().curtask);
    syscall::sleep();
  }
  
  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_outhdr {
    uint32 type;
    uint32 reserved;
    uint64 sector;
  } buf0;

  if(write)
    buf0.type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0.type = VIRTIO_BLK_T_IN; // read the disk
  buf0.reserved = 0;
  buf0.sector = sector;

  // buf0 is on a kernel stack, which is not direct mapped,
  // thus the call to kvmpa().
  disk.desc[idx[0]].addr = (uint64) &buf0; // @todo 直映射？
  disk.desc[idx[0]].len = sizeof(buf0);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = reinterpret_cast<uint64_t>(buf.d);
  disk.desc[idx[1]].len = bio::BlockBuf::blockSize;
  if(write)
    disk.desc[idx[1]].flags = 0; // device reads b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0;
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  volatile int ready=0;
  disk.info[idx[0]].ready = &ready;

  // avail[0] is flags
  // avail[1] tells the device how far to look in avail[2...].
  // avail[2...] are desc[] indices the device should process.
  // we only tell device the first index in our chain of descriptors.
  disk.avail[2 + (disk.avail[1] % NUM)] = idx[0];
  // __sync_synchronize();
  disk.avail[1] = disk.avail[1] + 1;

  mmio<platform::virtio::blk::MMIOInterface>(platform::virtio::blk::base)
    .queue.notify=0; // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
  // disk.info[idx[0]].waiting=kHartObj().curtask;
  /// @todo remove unused debug info
  xlen_t sip,sip1;
  csrRead(sip,sip);
  csrClear(sie,BIT(csr::mie::stie));
  csrSet(sstatus,BIT(csr::mstatus::sie));
  for(volatile int retry=std::numeric_limits<int>::max();ready == 0 && retry;retry--) {
    // syscall::sleep();
  }
  if(ready)success=1;
  else Log(error,"virtio_rw sector=%ld faild, retrying...",sector);
  csrClear(sstatus,BIT(csr::mstatus::sie));
  csrSet(sie,BIT(csr::mie::stie));
  csrRead(sip,sip1);
  // assert(((sip^sip1)&sip&0xff)==0);

  free_chain(idx[0]);
  }

  // release(&disk.vdisk_lock);
}
//0x8020f8a8
void
virtio_disk_intr()
{
  // acquire(&disk.vdisk_lock);
  Log(debug,"virtio::blk interrupt handler");

  while((disk.used_idx % NUM) != (disk.used->id % NUM)){
    Log(debug,"read id=%d, used->id=%d",disk.used_idx,disk.used->id);
    auto &info=disk.info[disk.used->elems[disk.used_idx].id];
    // @todo 未知错误
    // if(info.status != 0)
    //   panic("virtio_disk_intr status");
    
    *info.ready = 1;   // disk is done with buf
    // kGlobObjs->scheduler->wakeup(info.waiting);

    disk.used_idx = (disk.used_idx + 1) % NUM;
  }
  auto &interface=mmio<platform::virtio::blk::MMIOInterface>(platform::virtio::blk::base);
  interface.intr.ack=interface.intr.status & 0x3;

  // release(&disk.vdisk_lock);
}
