#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include "stdlib.h"
#include "sys/types.h"
#include "sys/mman.h"
#include "inttypes.h"

#define LEDCTRL     0x00000E00
#define LED0ON      0xE     //E in bits 3:0
#define LED0OFF     0xF     //F in bits 3:0
#define LED1ON      0xE00   //E in bits 11:8
#define LED1OFF     0xF00   //F in bits 11:8
#define LED2ON      0xE0000 //E in bits 19:16 
#define LED2OFF     0xF0000 //F in bits 19:16
#define PACKET      0x04074  
#define REC         0x100      

int main() {
    int         memfd;
    void        *baseAddr;
    uint32_t    *ledctrlAddr;
    uint32_t    *cntlAddr;
    uint32_t    *packetADDR;
    uint32_t    initialVal;
    uint32_t    packetVal;
    size_t      memWinSZ    = 0x0001F400;
    off_t       eth1Reg     = 0xF1200000;

    //Open the file and check to make sure it's valid
    if ((memfd = open("/dev/mem", O_RDWR)) < 0){
        printf("FD error %d : %d\n", memfd, errno);
        return -1;
    }

    baseAddr = mmap(NULL, memWinSZ, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, eth1Reg);    //Holy moly

    if(baseAddr == MAP_FAILED){     //Checks
        printf("Failed to Map\n");    
        close(memfd);               //Don't want that open anymore
        return -1;
    }

    ledctrlAddr = (uint32_t*)(baseAddr + LEDCTRL);   //Location to write to control LED's
    cntlAddr    = (uint32_t*)(baseAddr + REC);       //control addr
    packetADDR  = (uint32_t*)(baseAddr + PACKET);    //Good packet address

    //Initialize
    initialVal  = *ledctrlAddr;  
    *cntlAddr   = 0x2;              //Write 0x2 to enable

    printf("LED Control Initial Value: %08x \n", initialVal);   //print 32 bit address in hex

    *ledctrlAddr = LED2ON | LED1OFF | LED0ON;   // LED 0 and 2 turn on, led 1 is off
    printf("LED Control Value: %08x \n", *ledctrlAddr);   //print 32 bit address in hex
    sleep(2);

    *ledctrlAddr = LED2OFF | LED1OFF | LED0OFF;   // LED 0 and 2 turn on, led 1 is off
    printf("LED Control Value: %08x \n", *ledctrlAddr);   //print 32 bit address in hex
    sleep(2);

    printf("Looping LED's...\n");

    for(int i = 0; i < 5; ++i){
        *ledctrlAddr = LED2OFF | LED1OFF | LED0ON;   // LED 0, 1 and 2 turn on, led 1 is off
        sleep(1);

        *ledctrlAddr = LED2OFF | LED1ON | LED0OFF;   // LED 0 and 2 turn on, led 1 is off
        sleep(1);

        *ledctrlAddr = LED2ON | LED1OFF | LED0OFF;   // LED 0 and 2 turn on, led 1 is off
        sleep(1);
    }

    *ledctrlAddr = LED2OFF | LED1OFF | LED0OFF;   // LED 0 and 2 turn on, led 1 is off
    printf("Completed\n");

    *ledctrlAddr = initialVal;
    printf("Initial Value restored.\n");
    
    //Print the GPRC
    packetVal = *packetADDR;

    printf("Good Packets: %d \n", packetVal);

    if((munmap(baseAddr, memWinSZ)) < 0){
        printf("munmap failed : %d", errno);
        close(memfd);
        return -1;
    }

    close(memfd);

    return 0;
}

