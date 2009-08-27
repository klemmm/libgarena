/**
 * @mainpage
 * This is the main page. 
 */
 
#include <stdio.h>
#include <garena/config.h>
#include <garena/error.h>
#include <garena/gcrp.h>

FILE *deb;

int garena_init() {
  if (gcrp_init() == -1) {
    return -1;
  }
  if (gp2pp_init() == -1) {
    return -1;
  }
  if (ghl_init() == -1) {
    return -1;
  }
  deb = fopen(DEBUG_LOG, "w");
  if (deb == NULL) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  
  printf("Garena library initialized (version %s)\n", VERSION);
  return 0;
}
