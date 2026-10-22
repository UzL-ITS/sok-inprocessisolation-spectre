#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

  assert(_is_aligned(p));
  printf("[OK] aligned. ");

  assert(_is_tagged(p));
  printf("[OK] tagged. ");

  _check_access(p);
  printf("[OK] access. ");

  printf("\n");
  return p;
}

/********************************************************************
Benchmarking code.
********************************************************************/

void sort_in_place(uint64_t *data, size_t n) {
  // shell sort in place to obtain the median (no separate comparator needed)
  for (size_t gap = n / 2; gap > 0; gap /= 2) {
      for (size_t k = gap; k < n; ++k) {
        uint64_t tmp = data[k];
        size_t j;
        for (j = k; j >= gap && data[j - gap] > tmp; j -= gap) {
          data[j] = data[j - gap];
        }
        data[j] = tmp;
      }
    }
}

double get_stddev(uint64_t *data, size_t n, double mean) {
  // population variance / standard deviation
  double sq_sum = 0.0;
  for (size_t k = 0; k < n; ++k) {
    double diff = (double)data[k] - mean;
    sq_sum += diff * diff;
  }
  return sqrt(sq_sum / (double)n);
}

double get_median(uint64_t *data, size_t n) {
  sort_in_place(data, n);
  return (n & 1)
             ? (double)data[n / 2]
             : ((double)data[n / 2 - 1] + (double)data[n / 2]) / 2.0;
}

double get_q1(uint64_t *data, size_t n) {
  sort_in_place(data, n);
  double q1_pos = 0.25 * (double)(n - 1);
  size_t q1_lo = (size_t)q1_pos;
  return (q1_lo + 1 < n)
             ? (double)data[q1_lo] +
                   (q1_pos - (double)q1_lo) * ((double)data[q1_lo + 1] -
                                               (double)data[q1_lo])
             : (double)data[q1_lo];
}

double get_q3(uint64_t *data, size_t n) {
  sort_in_place(data, n);
  double q3_pos = 0.75 * (double)(n - 1);
  size_t q3_lo = (size_t)q3_pos;
  return (q3_lo + 1 < n)
             ? (double)data[q3_lo] +
                   (q3_pos - (double)q3_lo) * ((double)data[q3_lo + 1] -
                                               (double)data[q3_lo])
             : (double)data[q3_lo];
}

size_t remove_outliers(uint64_t *data, size_t n) {
  // discard samples that deviate more than 5 standard deviations from the
  // mean, so that rare large outliers (e.g. preemptions/interrupts during a
  // measurement) do not skew the reported statistics. retained samples are
  // compacted to the front of data; returns the number retained.
  double sum = 0.0;
  for (size_t k = 0; k < n; ++k)
    sum += (double)data[k];
  double mean = sum / (double)n;
  double threshold = 5.0 * get_stddev(data, n, mean);

  size_t kept = 0;
  for (size_t k = 0; k < n; ++k) {
    if (fabs((double)data[k] - mean) <= threshold)
      data[kept++] = data[k];
  }
  return kept > 0 ? kept : n;
}


#define ASSERT_DOMAIN(pointer, domain) (char *)((((uint64_t)(pointer)) & ~kPtrTagMask) | ((uint64_t)(domain) & kPtrTagMask))
enum benchmark { SSCA, TCA_SERIAL, STB_EXECPREV64, STB_EXECPREV4096 , N_BENCHMARKS };
char *benchmark_names[] = {"TME-Box", "TME-Box-Serial", "TME-Box-ExecPrev (64)", "TME-Box-ExecPrev (4096)"};

void benchmark_ssca(uint64_t *timings, void *data, void *public) {
  uint64_t t_start, t_end;

  volatile void *tmp;
  for (int i = 0; i < MEASUREMENTS; ++i) {

    t_start = rdtsc_begin();
    tmp = ASSERT_DOMAIN(data, public);
    t_end = rdtsc_end();

    timings[i] = t_end - t_start;
  }

}

void benchmark_tca_serial(uint64_t *timings) {
  uint64_t t_start, t_end;

  for (int i = 0; i < MEASUREMENTS; ++i) {

    t_start = rdtsc_begin();
    asm volatile("mfence");
    t_end = rdtsc_end();

    timings[i] = t_end - t_start;
  }

}

void benchmark_stb_execprev(uint64_t *timings, void *workload, size_t workload_size) {
  uint64_t t_start, t_end;

  for (int i = 0; i < MEASUREMENTS; ++i) {

    t_start = rdtsc_begin();
    mprotect(workload, workload_size, PROT_NONE);
    t_end = rdtsc_end();
    
    timings[i] = t_end - t_start;
    mprotect(workload, workload_size, PROT_READ | PROT_EXEC);
  }
  
}

void benchmark(enum benchmark bench) {
  #define PUBLIC "____________"

  size_t pagesize, workload_size;
  void *workload, *public, *data;

  size_t n_meas = MEASUREMENTS;
  uint64_t *timings = (uint64_t *)malloc(n_meas * sizeof(uint64_t));

  pagesize = sysconf(_SC_PAGESIZE);
  switch(bench) {
    case SSCA:
      data = check(malloc(128));
      public = check(malloc(128));
      memcpy(data, PUBLIC, strlen(PUBLIC));
      memcpy(public, PUBLIC, strlen(PUBLIC));
      benchmark_ssca(timings, data, public);
      free(public);
      free(data);
      break;
    case TCA_SERIAL:
      benchmark_tca_serial(timings);
      break;
    case STB_EXECPREV64:
      workload_size = 64 * pagesize;
      // workload = malloc(workload_size);
      workload = mmap(NULL, workload_size, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      benchmark_stb_execprev(timings, workload, workload_size);
      munmap(workload, workload_size);
      // free(workload);
      break;
    case STB_EXECPREV4096:
      workload_size = 4096 * pagesize;
      // workload = malloc(workload_size);
      workload = mmap(NULL, workload_size, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      benchmark_stb_execprev(timings, workload, workload_size);
      munmap(workload, workload_size);
      // free(workload);
      break;
    default:
      fprintf(stderr, "Unknown benchmark %d\n", bench);
      exit(EXIT_FAILURE);
  }

  // drop samples more than 5 standard deviations from the mean before
  // computing statistics
  size_t n_kept = remove_outliers(timings, n_meas);

  // single pass for sum / min / max
  uint64_t min_t = timings[0], max_t = timings[0];
  double sum = 0.0;
  for (size_t k = 0; k < n_kept; ++k) {
    uint64_t v = timings[k];
    sum += (double)v;
    if (v < min_t)
      min_t = v;
    if (v > max_t)
      max_t = v;
  }
  double mean = sum / (double)n_kept;

  double stddev = get_stddev(timings, n_kept, mean);
  double median = get_median(timings, n_kept);
  double q1 = get_q1(timings, n_kept);
  double q3 = get_q3(timings, n_kept);

  // print statistics about the timing measurements (iterations reflects the
  // total samples taken; the statistics are over the retained samples)
  printf("CSV:%s,%zu,%.2f,%.2f,%lu,%.2f,%.2f,%.2f,%lu\n", benchmark_names[bench], n_kept, mean, stddev, (unsigned long)min_t, q1, median, q3, (unsigned long)max_t);

  free(timings);
}

int main(int argc, char **argv) {

  setvbuf(stdout, NULL, _IOLBF, 0);

  printf("CSV:Category,Iterations,Mean,StdDev,Min,Q1,Median,Q3,Max\n");
  for (enum benchmark bench = 0; bench < N_BENCHMARKS; ++bench) {
    fprintf(stderr, "Running benchmark %s...\n", benchmark_names[bench]);
    benchmark(bench);
  }

}
