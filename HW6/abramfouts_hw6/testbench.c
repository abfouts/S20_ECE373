/*
 * Abram Fouts
 * 5/16/2020
 * ECE 373
 *
 * Homework 4 : Blinking LED on a timer
 */

#include "stdlib.h"
#include "stdio.h"
#include "errno.h"
#include "unistd.h"
#include "fcntl.h"

#define LEDON   0xE     //Mask with these
#define LEDOFF  0xF

int main() {

    int fd = open("/dev/ece_led", O_RDWR);
    char usrVar[10];
    int head, tail;

    if(fd < 0) {
        printf("Unknown File\n");
        return errno;
    } 

    //Read the file
    if(read(fd, &usrVar, sizeof(int)) < 0) {
        printf("Read Error\n");
        return errno;
    }
        
    //Print value that was read
    //printf("Value from the driver: %d\n", usrVar);

    //for (int i = 0; i < 3; ++i){    //Loop for different blink cycles :D
        /*
        //Get new valur to send
        printf("Please type a positive integer blink rate: ");
        scanf("%d", &usrVar);

        //write to the file
        if(write(fd, &usrVar, sizeof(int)) < 0) {
            printf("Write Error, try following the instructions\n"); 
            return errno;
        }
        */

        //Repeat to make sure the write happened
        //Read the file
        if(read(fd, &usrVar, sizeof(int)) < 0) {
            printf("Read Error\n");
            return errno;
        }
        
        head = (*usrVar >> 16);
        tail = (*usrVar & 0xFFFF);

        printf("Head: 0x%x\n", head);
        printf("Tail: 0x%x\n", tail);

        //Print value that was read
        //printf("Value from the driver: %d\n", usrVar);
    //}
    close(fd);
    return 0;
}