#include "sd.hh"
#include "kernel.hh"
#include "platform.h"

#define moduleLevel debug

namespace SD {
    __attribute__((optnone))
    void delay(int units){
        for(int i=0;i<units;i++);
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
    bool rxReady(){ checkInt();return mmio<RawIntStat>(base+rintst).intStat&RawIntStat::Kind::RxDR; }
    bool rxDone(){ checkInt();return mmio<RawIntStat>(base+rintst).intStat&RawIntStat::Kind::DtO; }
    typedef uint64_t resp_t;
    resp_t send_command(Command command,uint32_t arg=-1,int respLen=0){
        Log(info,"send cmd %x",*reinterpret_cast<uint32_t*>(&command));
        while(!cmdReady());
        Log(debug,"wait over");
        // wait for data ready
        // send arg then cmd
        if(arg!=-1){
            Log(info,"with arg=%x",arg);
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
    void recv(ByteArray &buf){
        Log(debug,"recv");
        int off=0;
        while(!rxDone()){
            delay(10000);
            while(!rxReady());
            while(mmio<Status>(base+status).fifo_count>=2){
                buf.buff[off++]=mmio<uint8_t>(base+0x600);
                Log(debug,"fifocnt=%d,off=%d",mmio<Status>(base+status).fifo_count,off);
            }
            checkInt();
        }
        Log(debug,"recv over");
    }

    bool init() {
        // static int irq_num = 33;  // @todo: trap里加了分支，但是搬过来的实现似乎没有intr相关函数
        Log(info,"begin sd init");
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
        if(!send_command(clkupd)) panic("error updating clock to disabled!");
        Log(info,"set low clock freq");
        mmio<uint16_t>(base+clkdiv)=125;
        Log(info,"renable clock");
        mmio<uint16_t>(base+clken)=1;
        if(!send_command(clkupd)) panic("error updating clock to low speed!");
        Log(info,"set card type");
        mmio<uint32_t>(base+ctype)=0;
        Log(info,"reset bus");
        mmio<BMod>(base+bmod)=BMod{.software_reset=1};
        Log(info,"reset fifo");

        mmio<Control>(base+ctrl)=Control{.fifo_reset=1,.dma_enable=0};
        mmio<uint8_t>(base+timeout)=0xff;
        Log(info,"timeout=%x",mmio<uint32_t>(base+timeout));
        Log(info,"send cmd0");
        if(auto resp=send_command(Command{.cmd_index=0,.send_initialization=1},0,1)){
            Log(info,"cmd0 resp=%x",resp);
        }
        Log(info,"setting up volt");
        if(auto resp=send_command(Command{.cmd_index=8,.wait_prvdata_complete=1,.use_hold_reg=1},(1<<8)|0xAA,1); resp&0xff==0xAA){
            Log(error,"cmd8 failed!");
        } else {
            Log(info,"volt set ok! resp=%x %d",resp,resp&0xff==0xAA);
        }

        Log(info,"send ACMD 41");
        if(auto resp=send_command(Command{.cmd_index=55,.wait_prvdata_complete=1,.use_hold_reg=1},0,1)){
            Log(info,"cmd55 resp=%x",resp);
        }
        if(auto resp=send_command(Command{.cmd_index=41,.use_hold_reg=1},(1<<30)|(1<<28)|(1<<24),1)){
            Log(info,"acmd41, resp=%x",resp);
        }
        while(true){
            if(auto resp=send_command(Command{.cmd_index=55,.wait_prvdata_complete=1,.use_hold_reg=1},0,1)){
                Log(info,"cmd55 resp=%x",resp);
            }
            if(auto resp=send_command(Command{.cmd_index=41,.use_hold_reg=1},(1 << 30)|(1<<28) | (1 << 24) | 0xFF8000,1); (resp&(1<<31))!=0){
                if(resp&(1<<30)!=0){
                    Log(error,"high cap card");
                    return false;
                }
                break;
            }
            delay(50000000);
        }

        if(auto resp=send_command(Command{.cmd_index=2,.wait_prvdata_complete=1,.use_hold_reg=1},0,4)){
            Log(info,"cid=%lx",resp);
        }
        resp_t rca;
        if(auto resp=send_command(Command{.cmd_index=3,.wait_prvdata_complete=1,.use_hold_reg=1},0,1)){
            rca=(resp>>16)&0xffff;
            Log(info,"rca=%x{%x}",rca,resp);
        }

        Log(info,"entering data transfer mode");

        // if(auto resp=send_command(Command{.cmd_index=9,.wait_prvdata_complete=1,.use_hold_reg=1},rca,4)){
        //     Log(info,"csd=%x",resp);
        // }
        
        Log(info,"put card to transfer state");
        send_command(Command{.cmd_index=7,.wait_prvdata_complete=1,.use_hold_reg=1},rca<<16,1);

        // Log(info,"try first read SCR");
        // send_command(Command{.cmd_index=55,.wait_prvdata_complete=1,.use_hold_reg=1},rca,1);
        // send_command(Command{.cmd_index=51,.wait_prvdata_complete=1,.use_hold_reg=1});
        Log(info, "try read one block");
        if(auto resp=send_command(Command{.cmd_index=17,.data_expected=1,.wait_prvdata_complete=1,.use_hold_reg=1},1,1)){
            Log(info,"cmd17 resp=%x",resp);
        }

        ByteArray buf(new uint8_t[1024],1024);
        recv(buf);
        delete[] buf.buff;
        return true;
    }

    bool read(uint64_t sector, uint8_t *buf, uint32_t bufsize) {
        // memcpy(buf, buffer, bufsize);
        return true;
    }

    bool write(uint64_t sector, uint8_t *buf, uint32_t bufsize) {
        // memcpy(buffer, buf, bufsize);
        return true;
    }
}