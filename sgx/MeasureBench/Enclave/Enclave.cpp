#include <stdarg.h>
#include <stdio.h>      /* vsnprintf */
#include <string.h>

#include "Enclave.h"
#include "Enclave_t.h"  /* print_string */

#define enclave_printf       printf

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

int printf(const char *fmt, ...)
{
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
    return 0;
}

int measure(uint64_t t_start){
    uint64_t tb_start;
    ocall_measure(&tb_start, t_start);
    uint64_t t_end = rdtsc();
    enclave_printf("[ocall][u->t]: %lu\n", t_end - tb_start);
    
    return 0;
}

uint64_t ecall_func1(uint64_t t_start)
{
    uint64_t t_end = rdtsc();
    enclave_printf("[ecall][u->t]: %lu\n", t_end - t_start);
    
    measure(rdtsc());

    //enclave_printf("Done!\n");
    return rdtsc();

}
