#include "fs.hh"
#include "vm.hh"
#include "kernel.hh"
#include "klib.h"

#define FMT_PROC(fmt,...) "Proc[%d]::"#fmt"\n",kHartObjs.curtask->getProcess()->pid(),__VA_ARGS__

using namespace fs;

void File::write(xlen_t addr,size_t len){
    if(!ops.fields.w)return ;
    auto bytes=kHartObjs.curtask->getProcess()->vmar.copyin(addr,len);
    switch(type){
        case FileType::stdout:
        case FileType::stderr:
            printf(FMT_PROC("%s",bytes.c_str()));
            break;
        case FileType::pipe:
            obj.pipe->write(bytes);
            break;
        default:
            break;
    }
}

File::~File() {
    switch(type){
        case FileType::pipe: {
            obj.pipe.deRef();
            break;
        }
        case FileType::inode: {
            obj.inode.deRef();
            /*关闭逻辑*/
            break;
        }
    }
}

static struct {
    uint32 first_data_sec; // data所在的第一个扇区
    uint32 data_sec_cnt; // 数据扇区数
    uint32 data_clus_cnt; // 数据簇数
    uint32 byts_per_clus; // 每簇字节数
    struct {
        uint16  byts_per_sec;  // 扇区字节数
        uint8   sec_per_clus;  // 每簇扇区数
        uint16  rsvd_sec_cnt;  // 保留扇区数
        uint8   fat_cnt;  // fat数          
        uint32  hidd_sec;  // 隐藏扇区数         
        uint32  tot_sec;  // 总扇区数          
        uint32  fat_sz;   // 一个fat所占扇区数           
        uint32  root_clus; // 根目录簇号 
    } bpb;
} fat; 

int fat32_init() {
    // struct buf *b = bread(0, 0);
    // if (strncmp((char const*)(b->data + 82), "FAT32", 5)) { panic("not FAT32 volume"); }
    // // fat.bpb.byts_per_sec = *(uint16 *)(b->data + 11);
    // memmove(&fat.bpb.byts_per_sec, b->data + 11, 2); // avoid misaligned load on k210
    // fat.bpb.sec_per_clus = *(b->data + 13);  
    // fat.bpb.rsvd_sec_cnt = *(uint16 *)(b->data + 14);
    // fat.bpb.fat_cnt = *(b->data + 16);
    // fat.bpb.hidd_sec = *(uint32 *)(b->data + 28);
    // fat.bpb.tot_sec = *(uint32 *)(b->data + 32);
    // fat.bpb.fat_sz = *(uint32 *)(b->data + 36);
    // fat.bpb.root_clus = *(uint32 *)(b->data + 44);
    // fat.first_data_sec = fat.bpb.rsvd_sec_cnt + fat.bpb.fat_cnt * fat.bpb.fat_sz;
    // fat.data_sec_cnt = fat.bpb.tot_sec - fat.first_data_sec;
    // fat.data_clus_cnt = fat.data_sec_cnt / fat.bpb.sec_per_clus;
    // fat.byts_per_clus = fat.bpb.sec_per_clus * fat.bpb.byts_per_sec;
    // // brelse(b);

    // // make sure that byts_per_sec has the same value with BSIZE 
    // if (BSIZE != fat.bpb.byts_per_sec) 
    //     panic("byts_per_sec != BSIZE");
    // initlock(&ecache.lock, "ecache");
    // memset(&root, 0, sizeof(root));
    // initsleeplock(&root.lock, "entry");
    // root.attribute = (ATTR_DIRECTORY | ATTR_SYSTEM); 
    // root.first_clus = root.cur_clus = fat.bpb.root_clus;
    // root.valid = 1; 
    // root.prev = &root;
    // root.next = &root;
    // root.filename[0] = '/';
    // root.filename[1] = '\0';
    // for(struct dirent *de = ecache.entries; de < ecache.entries + ENTRY_CACHE_NUM; de++) {
    //     de->dev = 0;
    //     de->valid = 0;
    //     de->ref = 0;
    //     de->dirty = 0;
    //     de->parent = 0;
    //     de->next = root.next;
    //     de->prev = &root;
    //     initsleeplock(&de->lock, "entry");
    //     root.next->prev = de;
    //     root.next = de;
    // }
    //  //将主存的系统存入设备管理
    // dev_fat[0].byts_per_clus=fat.byts_per_clus;
    // dev_fat[0].data_clus_cnt=fat.data_clus_cnt;
    // dev_fat[0].data_sec_cnt=fat.data_sec_cnt;
    // dev_fat[0].first_data_sec=fat.first_data_sec;
    // dev_fat[0].bpb.byts_per_sec=fat.bpb.byts_per_sec;
    // dev_fat[0].bpb.fat_cnt=fat.bpb.fat_cnt;
    // dev_fat[0].bpb.fat_sz=fat.bpb.fat_sz;
    // dev_fat[0].bpb.hidd_sec=fat.bpb.hidd_sec;
    // dev_fat[0].bpb.root_clus=fat.bpb.root_clus;
    // dev_fat[0].bpb.rsvd_sec_cnt=fat.bpb.rsvd_sec_cnt;
    // dev_fat[0].bpb.sec_per_clus=fat.bpb.sec_per_clus;
    // dev_fat[0].bpb.tot_sec=dev_fat->bpb.tot_sec;
    // dev_fat[0].mount_mode=0;
    // dev_fat[0].root=root;
    // dev_fat[0].vaild=1;
    // return 0;
    // return 0;
}
