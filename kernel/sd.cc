#include "sd.hh"
#include "kernel.hh"
#include "platform.h"

#define moduleLevel debug

namespace SD {
    static uintptr_t __base = 0;

    static bool host_use_dma = true;
    static bool host_use_adma = false;
    static bool host_use_64_bit_dma = false;
    static int  host_sdma_boundary = 0;
    static int  data_blksz = 512;
    static int  data_blocks = 0;

    static uint32_t response_data[4];
    static uint8_t buffer[4096] __attribute((aligned(4096)));

    static void __attribute((optnone)) delay(int units) {
        for (int i = 0; i < units; i++) {}
    }

    template <typename T>
    static inline volatile T *reg(int off) {
        return (volatile T *)(__base + off);
    }

    static inline uint16_t make_command(int command_index, int &data_present, int &response_type, int &command_index_check, int &command_crc_check, int &command_type) {
        switch (command_index) {
            case 0:
                response_type = SDHCI_CMD_RESP_NONE;
                break;
            case 17:
            case 18:
            case 24:
            case 25:
                data_present = 1;
                break;
            default:
                break;
        }
        if (command_type == -1) {
            command_type = 0;
        }
        if (data_present == -1) {
            data_present = 0;
        }
        if (response_type == -1) {
            response_type = SDHCI_CMD_RESP_SHORT;
        }
        if (command_index_check == -1) {
            if (response_type == SDHCI_CMD_RESP_SHORT || response_type == SDHCI_CMD_RESP_SHORT_BUSY) {
                command_index_check = 1;
            } else {
                command_index_check = 0;
            }
        }
        if (command_crc_check == -1) {
            if ((response_type == SDHCI_CMD_RESP_SHORT && command_index_check == 1) || response_type == SDHCI_CMD_RESP_SHORT_BUSY || response_type == SDHCI_CMD_RESP_LONG) {
                command_crc_check = 1;
            } else {
                command_crc_check = 0;
            }
        }
        return (command_index << 8) | (command_type << 6) | (data_present << 5) | (command_index_check << 4) | (command_crc_check << 3) | (response_type);
    }

    static inline bool send_command(int command_index, uint32_t argument = 0, int data_present = -1, int response_type = -1, int command_index_check = -1, int command_crc_check = -1, int command_type = 0) {
        uint16_t command = make_command(command_index, data_present, response_type, command_index_check, command_crc_check, command_type);
        if ((*reg<uint32_t>(SDHCI_PRESENT_STATE) & SDHCI_CMD_INHIBIT) != 0) return false;
        if (data_present) {
            if ((*reg<uint32_t>(SDHCI_PRESENT_STATE) & SDHCI_DATA_INHIBIT) != 0) return false;
            // set timeout
            *reg<uint32_t>(SDHCI_INT_ENABLE) = *reg<uint32_t>(SDHCI_INT_ENABLE) | SDHCI_INT_TIMEOUT;
            *reg<uint32_t>(SDHCI_SIGNAL_ENABLE) = *reg<uint32_t>(SDHCI_SIGNAL_ENABLE) | SDHCI_INT_TIMEOUT;
            // config dma
            uint8_t ctrl = *reg<uint8_t>(SDHCI_HOST_CONTROL);
            ctrl &= ~SDHCI_CTRL_DMA_MASK;
            if (host_use_dma) {
                if (host_use_adma) {
                    ctrl |= SDHCI_CTRL_ADMA32;
                }
                if (host_use_64_bit_dma) {
                    ctrl |= SDHCI_CTRL_ADMA64;
                }
            }
            *reg<uint8_t>(SDHCI_HOST_CONTROL) = ctrl;
            // pre dma transfer
            // NOP
            // *reg<uint32_t>(SDHCI_DMA_ADDRESS) = kva2pa((uintptr_t)buffer);
            *reg<uint32_t>(SDHCI_DMA_ADDRESS) = (uintptr_t)buffer;  // @检查？
            // set transfer irqs
            uint32_t pio_irqs = SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL;
            uint32_t dma_irqs = SDHCI_INT_DMA_END | SDHCI_INT_ADMA_ERROR;
            uint32_t ier = *reg<uint32_t>(SDHCI_INT_ENABLE);
            if (host_use_dma) {
                ier = (ier & ~pio_irqs) | dma_irqs;
            } else {
                ier = (ier & ~dma_irqs) | pio_irqs;
            }
            *reg<uint32_t>(SDHCI_INT_ENABLE) = ier;
            *reg<uint32_t>(SDHCI_SIGNAL_ENABLE) = ier;
            // set block info
            *reg<uint16_t>(SDHCI_BLOCK_SIZE) = SDHCI_MAKE_BLKSZ(host_sdma_boundary, data_blksz);
            *reg<uint16_t>(SDHCI_BLOCK_COUNT) = data_blocks;
        }
        *reg<uint32_t>(SDHCI_ARGUMENT) = argument;
        // set transfer mode
        uint16_t mode = *reg<uint16_t>(SDHCI_TRANSFER_MODE);
        if (!data_present) {
            if (false) {
                mode = 0;
            } else {
                mode = mode & ~(SDHCI_TRNS_AUTO_CMD12 | SDHCI_AUTO_CMD23);
            }
        } else {
            mode = SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN;
            if (data_blocks > 1) {
                mode |= SDHCI_TRNS_MULTI;
                mode |= SDHCI_AUTO_CMD12;
            }
            if (command_index == 17 || command_index == 18) {
                mode |= SDHCI_TRNS_READ;
            }
        }
        *reg<uint16_t>(SDHCI_TRANSFER_MODE) = mode;
        *reg<uint16_t>(SDHCI_COMMAND) = command;
        return true;
    }

    static bool finish_command(int command_index, int data_present = -1, int response_type = -1, int command_index_check = -1, int command_crc_check = -1, int command_type = -1) {
        make_command(command_index, data_present, response_type, command_index_check, command_crc_check, command_type);
        bool command_complete = false;
        for (int i = 0; i < 100; i++) {
            if ((*reg<uint32_t>(SDHCI_INT_STATUS) & SDHCI_INT_ERROR_MASK) != 0) {
                uint32_t error_status = *reg<uint32_t>(SDHCI_INT_STATUS) & SDHCI_INT_ERROR_MASK;
                Log(error, "Command Error %d\n", error_status);
                return false;
            }
            if ((*reg<uint32_t>(SDHCI_INT_STATUS) & SDHCI_RESPONSE) != 0) {
                command_complete = true;
                break;
            }
            delay(10000);
        }
        if (command_complete) {
            *reg<uint32_t>(SDHCI_INT_STATUS) = SDHCI_INT_RESPONSE; // write 1 to clear
        } else {
            Log(error, "Command Complete not received\n");
            return false;
        }
        if (data_present) {
            bool transfer_complete = false;
            for (int i = 0; i < 100; i++) {
                if ((*reg<uint32_t>(SDHCI_INT_STATUS) & SDHCI_INT_ERROR_MASK) != 0) {
                    uint32_t error_status = *reg<uint32_t>(SDHCI_INT_STATUS) & SDHCI_INT_ERROR_MASK;
                    Log(error, "Command Error %d\n", error_status);
                    return false;
                }
                if ((*reg<uint32_t>(SDHCI_INT_STATUS) & SDHCI_INT_DATA_END) != 0) {
                    transfer_complete = true;
                    break;
                }
                delay(10000);
            }
            if (transfer_complete) {
                *reg<uint32_t>(SDHCI_INT_STATUS) = SDHCI_INT_DATA_END; // write 1 to clear
            } else {
                Log(error, "Transfer Complete not received\n");
                return false;
            }
        }
        if (response_type == SDHCI_CMD_RESP_LONG) {
            for (int i = 0; i < 4; i++) {
                response_data[i] = reg<uint32_t>(SDHCI_RESPONSE)[i];
            }
        } else {
            response_data[0] = reg<uint32_t>(SDHCI_RESPONSE)[0];
        }
        return true;
    }

    static bool card_reset() { // CMD0
        send_command(0);
        return true;
    }

    static bool send_if_cond() { // CMD8
        send_command(8, 0x1aa);
        return finish_command(8);
    }

    static bool send_op_cond() { // ACMD41
        send_command(55);
        if (!finish_command(55)) return false;
        send_command(41);
        return finish_command(41);
    }

    static bool read_single_block(uint32_t data_address, uint8_t *buf) { // CMD17
        data_blocks = 1;
        send_command(17, data_address);
        return finish_command(17);
    }

    static bool read_multiple_block(uint32_t data_address, uint8_t *buf, uint16_t block_count) { // CMD18
        data_blocks = block_count;
        send_command(18, data_address);
        return finish_command(18);
    }

    static bool write_single_block(uint32_t data_address, uint8_t *buf) { // CMD24
        data_blocks = 1;
        send_command(24, data_address);
        return finish_command(24);
    }

    static bool write_multiple_block(uint32_t data_address, uint8_t *buf, uint16_t block_count) { // CMD25
        data_blocks = block_count;
        send_command(25, data_address);
        return finish_command(25);
    }
    using namespace platform::sdio;
    bool cmdReady(){ return mmio<Command>(base+cmd).start_cmd==0;}
    bool cmdDone(){ return mmio<RawIntStat>(base+rintst).intStat&RawIntStat::Kind::CD;}
    void checkInt(){
        auto intstat=mmio<RawIntStat>(base+rintst);
        Log(debug,"intstat=%x",mmio<uint16_t>(base+rintst));
        mmio<RawIntStat>(base+rintst)=intstat;
        Log(debug,"intstat=%x",mmio<uint16_t>(base+rintst));
    }
    typedef uint64_t resp_t;
    resp_t send_command(Command command,uint32_t arg=0,int respLen=0){
        Log(info,"send cmd %x",*reinterpret_cast<uint32_t*>(&command));
        while(!cmdReady());
        Log(debug,"wait over");
        // wait for data ready
        // send arg then cmd
        if(arg){
            mmio<uint32_t>(base+cmdarg)=arg;
        }
        command.card_number=0;
        command.start_cmd=1;
        command.response_expected=respLen>0;
        command.response_length=respLen>1;

        mmio<Command>(base+cmd)=command;
        // wait for cmd accepted
        while(!cmdReady());
        // wait for resp
        resp_t resp=0;
        if(respLen){
            while(!cmdDone());
            resp=mmio<uint32_t>(base+resp0);
            if(respLen>1){
                resp|=((resp_t)mmio<uint32_t>(base+resp1))<<32;
                resp|=((resp_t)mmio<uint32_t>(base+resp2))<<64;
                resp|=((resp_t)mmio<uint32_t>(base+resp3))<<96;
            }
            Log(debug,"recv resp=%x",resp);
        } else {
            resp=true;
        }
        checkInt();
        // wait for data valid
        return resp;
    }

    bool init() {
        // static int irq_num = 33;  // @todo: trap里加了分支，但是搬过来的实现似乎没有intr相关函数
        Log(info,"begin sd init");
        __base = base;
        // enable_interrupt();
        // csrClear(sie,BIT(csr::mie::stie));
        // csrSet(sstatus,BIT(csr::mstatus::sie));  // @todo: 启用中断
        Log(info,"card detect %x",mmio<uint32_t>(base+cardDetect));
        Log(info,"disable clock");
        mmio<uint16_t>(base+clken)=0;
        Command clkupd={
            .wait_prvdata_complete=1,
            .update_clock_register_only=1,
            .start_cmd=1
        };
        if(!send_command(cmd)) panic("error updating clock to disabled!");
        Log(info,"set low clock freq");
        mmio<uint16_t>(base+clkdiv)=255;
        Log(info,"renable clock");
        mmio<uint16_t>(base+clken)=1;
        if(!send_command(cmd)) panic("error updating clock to low speed!");
        Log(info,"set card type");
        mmio<uint32_t>(base+ctype)=0;
        Log(info,"reset bus");
        mmio<BMod>(base+bmod)=BMod{.software_reset=1};
        Log(info,"reset fifo");

        mmio<Control>(base+ctrl)=Control{.fifo_reset=1,.dma_enable=0};
        Log(info,"send cmd0");
        send_command(Command{.cmd_index=0,.send_initialization=1});
        Log(info,"setting up volt");
        if(auto resp=send_command(Command{.cmd_index=8,.wait_prvdata_complete=1,.use_hold_reg=1},(1<<8)|0xAA,1); resp&0xAA==0){
            Log(error,"cmd8 failed!");
        } else {
            Log(info,"volt set ok! resp=%x %d",resp,resp&0xAA==0);
        }

        Log(info,"send ACMD 41");
        send_command(Command{.cmd_index=55,.wait_prvdata_complete=1,.use_hold_reg=1});
        if(auto resp=send_command(Command{.cmd_index=41,.wait_prvdata_complete=1,.use_hold_reg=1},(1 << 30) | (1 << 24) | 0xFF8000,1); resp&(1<<30)!=0){
            Log(error,"high cap card");
            return false;
        }
        delay(500000000);

        if(auto resp=send_command(Command{.cmd_index=2,.wait_prvdata_complete=1,.use_hold_reg=1},0,4)){
            Log(info,"cid=%lx",resp);
        }
        

        // *reg<uint8_t>(SDHCI_TIMEOUT_CONTROL) = 0;
        // *reg<uint8_t>(SDHCI_POWER_CONTROL) = 0;
        // Log(debug,"setting up power");
        // *reg<uint8_t>(SDHCI_POWER_CONTROL) = SDHCI_POWER_ON | SDHCI_POWER_330;
        // *reg<uint8_t>(SDHCI_HOST_CONTROL) |= SDHCI_CTRL_HISPD;
        // *reg<uint8_t>(SDHCI_POWER_CONTROL) = SDHCI_POWER_ON | SDHCI_POWER_180;
        // *reg<uint16_t>(SDHCI_CLOCK_CONTROL) = (4 << SDHCI_DIVIDER_SHIFT);
        // mmio<uint16_t>(base+SDHCI_CLOCK_CONTROL) |= SDHCI_CLOCK_CARD_EN;


        // Log(debug,"write cmd0 to %x",base+SDHCI_COMMAND);
        // mmio<uint16_t>(base+SDHCI_COMMAND)=0 | SDHCI_CMD23_ENABLE;
        // // while(!mmio<uint16_t>(base+SDHCI_STAte))
        // Log(debug,"cmd0 written, reset timeout");
        // // *reg<uint8_t>(SDHCI_SOFTWARE_RESET) = SDHCI_RESET_ALL;
        // // while (*reg<uint8_t>(SDHCI_SOFTWARE_RESET) != 0);
        // uint32_t ier = SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
        //     SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT |
        //     SDHCI_INT_INDEX | SDHCI_INT_END_BIT | SDHCI_INT_CRC |
        //     SDHCI_INT_TIMEOUT | SDHCI_INT_DATA_END |
        //     SDHCI_INT_RESPONSE;
        // *reg<uint32_t>(SDHCI_INT_ENABLE) = ier;
        // *reg<uint32_t>(SDHCI_SIGNAL_ENABLE) = ier;
        // // SD Card Detection
        // *reg<uint16_t>(SDHCI_INT_ENABLE) = *reg<uint16_t>(SDHCI_INT_ENABLE) | SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE;
        // *reg<uint16_t>(SDHCI_SIGNAL_ENABLE) = *reg<uint16_t>(SDHCI_INT_ENABLE) | SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE;
        // delay(1000000);
        // if (*reg<uint32_t>(SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT) {
        //     Log(error, "SD Card inserted\n");
        // } else {
        //     Log(error, "SD Card removed\n");
        //     return false;
        // }
        // // Card Initialization and Identification
        // Log(info, "Sending CARD_RESET\n");
        // card_reset();
        // delay(30000);
        // Log(info, "Sending IF_COND\n");
        // send_if_cond();
        // Log(info, "Sending OP_COND\n");
        // send_op_cond();
        // Log(info, "SD Init DONE\n");
        // disable_interrupt();
        // csrClear(sstatus,BIT(csr::mstatus::sie)); // @todo: 禁用中断
        // csrSet(sie,BIT(csr::mie::stie));
        return true;
    }

    bool read(uint64_t sector, uint8_t *buf, uint32_t bufsize) {
        assert(__base != 0);
        // enable_interrupt();
        // csrClear(sie,BIT(csr::mie::stie));
        // csrSet(sstatus,BIT(csr::mstatus::sie));  // @todo: 启用中断
        if (bufsize <= 512) {
            // return read_single_block(sector, (uint8_t *)kva2pa((uintptr_t)buffer));
            return read_single_block(sector, (uint8_t *)(uintptr_t)buffer);  // @todo: 检查？
        } else {
            return read_multiple_block(sector, (uint8_t *)(uintptr_t)buffer, (bufsize + 511) / 512);
        }
        // disable_interrupt();
        // csrClear(sstatus,BIT(csr::mstatus::sie)); // @todo: 禁用中断
        // csrSet(sie,BIT(csr::mie::stie));
        memcpy(buf, buffer, bufsize);
        return true;
    }

    bool write(uint64_t sector, uint8_t *buf, uint32_t bufsize) {
        assert(__base != 0);
        memcpy(buffer, buf, bufsize);
        // enable_interrupt();
        // csrClear(sie,BIT(csr::mie::stie));
        // csrSet(sstatus,BIT(csr::mstatus::sie));  // @todo: 启用中断
        if (bufsize <= 512) {
            return write_single_block(sector, (uint8_t *)(uintptr_t)buffer);
        } else {
            return write_multiple_block(sector, (uint8_t *)(uintptr_t)buffer, (bufsize + 511) / 512);
        }
        // csrClear(sstatus,BIT(csr::mstatus::sie)); // @todo: 禁用中断
        // csrSet(sie,BIT(csr::mie::stie));
    }
}