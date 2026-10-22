#include <stdarg.h>
#include <stdio.h>      /* vsnprintf */
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "Enclave.h"
#include "Enclave_t.h"  /* print_string */

#define enclave_printf       printf

//#define DEBUG_PRINT
#define SECRET "INACCESSIBLESECRET"

int printf(const char *fmt, ...)
{
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
    return 0;
}

unsigned char * create_secret() {
  // allocate secret
  unsigned char* secret = (unsigned char*)malloc(sizeof(unsigned char) * 128);

  if(secret == NULL) {
    enclave_printf("allocation of secret failed\n");
  }
  memcpy(secret, SECRET, sizeof(SECRET));

  return secret;
}

uint64_t ecall_start_victim(){
  unsigned char* data = create_secret(); 
  uint64_t addr = (uint64_t) data;

  ocall_attack(addr);

  return (uint64_t)SGX_SUCCESS;
}