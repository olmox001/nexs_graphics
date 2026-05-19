/* reimplementation/kernel/include/ctype.h — extends base-nexs/kernel ctype.h */
#ifndef KERNEL_CTYPE_H
#define KERNEL_CTYPE_H
int isspace (int c);
int isdigit (int c);
int isalpha (int c);
int isalnum (int c);
int isprint (int c);
int isupper (int c);
int islower (int c);
int isxdigit(int c);
int tolower (int c);
int toupper (int c);
#endif
