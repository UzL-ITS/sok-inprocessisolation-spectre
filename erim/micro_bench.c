#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <common.h>
#include <errno.h>

#include "spex.h"
#define ERIM_DBG 1
#define SECRET "INACCESSIBLE SECRET"

#ifndef BENCHMARK_SIZE 
  #define BENCHMARK_SIZE 64
#endif

uint64_t rdtsc() {
  uint64_t a, d;
  asm volatile("mfence");
#if USE_RDTSCP
  asm volatile("rdtscp" : "=a"(a), "=d"(d) :: "rcx");
#else
  asm volatile("rdtsc" : "=a"(a), "=d"(d));
#endif
  a = (d << 32) | a;
  asm volatile("mfence");
  return a;
}

unsigned long read_var(unsigned long * var) {
  return *var;
}

unsigned char * create_secret() {
  // init isolation and sh mem
  if(erim_init(4096*128, ERIM_FLAG_ISOLATE_TRUSTED)) {
    exit(EXIT_FAILURE);
  }
  // scanmem for wrpkru
  if(erim_memScan(NULL, NULL, ERIM_UNTRUSTED_PKRU)) {
    exit(EXIT_FAILURE);
  }

  // allocate secret
  unsigned char* secret = erim_malloc(sizeof(unsigned char) * 128);

  if(secret == NULL) {
    printf("allocation of secret failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(secret, SECRET, sizeof(SECRET));
  secret[sizeof(SECRET)] = '0';
  erim_switch_to_untrusted;

  return secret;
}

int main(int argc, char **argv) {

    #define SIZE BENCHMARK_SIZE

    size_t pagesize = 4096;

    char* _mem = mmap(NULL, pagesize * SIZE, PROT_WRITE | PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);//(char*)malloc(pagesize * (SIZE+10));
    if (!_mem){
        printf("%d",errno);
        return 0;
    }

    char* someCode = _mem;

    memset(someCode, '.', 4096 * (SIZE));

    printf("%p\n", someCode);

    setupTAndU(0,0,(uint8_t*)someCode, SIZE * pagesize);

    unsigned char* data = create_secret();
    printf("%p\n", data);

    #define ITER 10000

    uint64_t times_1[ITER]; 
    uint64_t times_2[ITER];
    uint64_t times_3[ITER];
    uint64_t times_4[ITER];

    uint64_t start, end;


    for (int i = 0; i < ITER; i++){
        start = rdtsc();
        erim_switch_to_trusted;
        end = rdtsc();
        times_1[i] = end - start;

        char c = read_var(data);

        start = rdtsc();
        erim_switch_to_untrusted;
        end = rdtsc();
        times_2[i] = end - start;


        start = rdtsc();
        Switch_T;
        end = rdtsc();
        times_3[i] = end - start;
        
        c = read_var(data);

        start = rdtsc();
        Switch_U;
        end = rdtsc();
        times_4[i] = end - start;
    }

    
    for (int i = 0; i < ITER; i++){
        printf("[ERIM:U->T (%d)]: %ld\n", SIZE, times_1[i]);
        printf("[ERIM:T->U (%d)]: %ld\n", SIZE, times_2[i]);
        printf("[ERIM+exec:U->T (%d)]: %ld\n", SIZE, times_3[i]);
        printf("[ERIM+exec:T->U (%d)]: %ld\n", SIZE, times_4[i]);
    }


    return SWS_SUCCESS;
}