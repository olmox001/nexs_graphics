/* reimplementation/kernel/include/stdlib.h — extends base-nexs/kernel stdlib.h */
#ifndef KERNEL_STDLIB_H
#define KERNEL_STDLIB_H
#include <stddef.h>
void      *malloc  (size_t size);
void      *calloc  (size_t nmemb, size_t size);
void      *realloc (void *ptr, size_t size);
void       free    (void *ptr);
int        atoi    (const char *nptr);
long long  atoll   (const char *nptr);
double     atof    (const char *nptr);
long       strtol  (const char *nptr, char **endptr, int base);
long long  strtoll (const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
double     strtod  (const char *nptr, char **endptr);
void       exit    (int status) __attribute__((noreturn));
char      *getenv  (const char *name);
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#endif
