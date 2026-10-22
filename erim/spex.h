#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <erim.h>

#define MSR_IA32_PRED_CMD 0x49

#ifndef EXEC_PREV
  #define EXEC_PREV 0
#endif

#if EXEC_PREV == 1
    #define Switch_T switchT(1);
    #define Switch_U switchU();
    #define Switch_TX switchT(0);
#else
    #define Switch_T erim_switch_to_trusted;
    #define Switch_U erim_switch_to_untrusted;
#endif

uint8_t *addrT, *addrU;
uint64_t sizeT, sizeU;

void write_msr(uint32_t msr, uint64_t value) {
    char msr_filename[32];
    int fd;

    // Construct the file path for the MSR of the specified CPU
    sprintf(msr_filename, "/dev/cpu/%d/msr", 0);

    // Open the MSR device file
    fd = open(msr_filename, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open MSR device file");
        return;
    }

    // Write the value to the MSR
    if (pwrite(fd, &value, sizeof(value), msr) != sizeof(value)) {
        perror("Failed to write to MSR");
    }

    close(fd);
}

void setupTAndU(uint8_t* fT0, uint8_t* fTn, uint8_t* fU0, uint8_t* fUn){
    addrT = fT0;
    addrU = fU0;

    sizeT = (((uint64_t)(fTn - fT0)) + 4096) & (~(4096-1));
    sizeU = (((uint64_t)(fUn - fU0)) + 4096) & (~(4096-1));
    //write_msr(MSR_IA32_PRED_CMD, 1);
}

void switchT(int i){
    erim_switch_to_trusted;
    //write_msr(MSR_IA32_PRED_CMD, 1);
    mprotect(addrU, (size_t)sizeU, PROT_NONE);
}


void switchU(){
    erim_switch_to_untrusted;
    //write_msr(MSR_IA32_PRED_CMD, 1);
    mprotect(addrU, (size_t)sizeU, PROT_EXEC | PROT_READ);
}