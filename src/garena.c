/**
 * @mainpage
 * This is the main page. 
 */
 
#include <stdio.h>
#include <garena/config.h>
#include <garena/error.h>
#include <garena/gcrp.h>

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
  printf("Garena library initialized (version %s)\n", VERSION);
  return 0;
}
