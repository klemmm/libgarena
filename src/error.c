#include <stdio.h>
#include <errno.h>
#include <garena/error.h>
#include <garena/garena.h>

long garena_errno = 0;

static char *errstr[] = {
  "Success",
  "Not implemented", 
  "Invalid Argument",
  "Temporary failure, try again",
  "LIBC error (this message should not appear)",
  "Unknown/undocumented error",
  "Resource is already in use",
  "Object not found",
  "Protocol error",
  "Insufficient resource"
}; 
   
/**
 * Display human-readable error message
 *
 * @param msg Message prefix
 */
void garena_perror(char *msg) {
  if (garena_errno == GARENA_ERR_LIBC) {
    fprintf(deb, "%s: libc error: %s\n", msg, strerror(errno));
  } else {
    fprintf(stderr, "%s: garena error: %s\n", msg, errstr[-garena_errno]);
  }
}  

