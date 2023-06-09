#ifndef GARENA_ERROR_H
#define GARENA_ERROR_H 1

extern long garena_errno;

#define GARENA_OK 0
#define GARENA_ERROR -1

#define GARENA_ERR_SUCCESS 0
#define GARENA_ERR_NOTIMPL -1
#define GARENA_ERR_INVALID -2
#define GARENA_ERR_AGAIN -3
#define GARENA_ERR_LIBC -4
#define GARENA_ERR_UNKNOWN -5
#define GARENA_ERR_INUSE -6
#define GARENA_ERR_NOTFOUND -7
#define GARENA_ERR_PROTOCOL -8
#define GARENA_ERR_NORESOURCE -9
char *garena_strerror();
void garena_perror(char *msg);


#endif
