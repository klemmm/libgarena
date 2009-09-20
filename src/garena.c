/**
 * @mainpage
 *
 * The library is event-driven. You use functions to start operations (connect server, join room, ...)
 * and to install event handlers. You receive events when operations finish (joining room completed or aborted, ...),
 * or when something happens on Garena (someone talks on channel, you receive a game VPN packet, ...). 
 * All events are received with an Event Data structure (which depends on the type of the event). 
  *
 * @par To initialize and de-initialize the library, look for functions in:
 * @li @ref garena.c
 * @par Protocol-related garena functions and event descriptions are in:
 * @li @ref ghl.c
 * @li @ref ghl.h
 * @par Some useful misc functions are located in:
 * @li @ref util.c
 * @par Basically, to use the library to create a client, the simplest way is:
 * @li Initialize using @ref garena_init
 * @li Connect to server using @ref ghl_new_serv (and so, get a server handle)
 * @li Install handlers for various events with @ref ghl_register_handler
 * @li Run a main loop that calls @ref ghl_process in blocking mode (look up function doc to find out what the modes are)
 * @li If you want to multiplex Garena and others file descriptors with a select() in your main loop, look up the functions @ref ghl_fill_fds and @ref ghl_fill_tv (and use @ref ghl_process in nonblocking mode)
 */

/**
 * @file
 * 
 * This file contains various generic, non protocol-related, functions for garena.
 *
 */
  
#include <stdio.h>
#include <signal.h>
#include <garena/config.h>
#include <garena/error.h>
#include <garena/gcrp.h>
#include <garena/gsp.h>
#include <garena/gp2pp.h>
#include <garena/ghl.h>
#include <garena/private.h>

FILE *deb;

/**
 * Call this function to free the library memory structures
 */
void garena_fini() {
  ghl_fini();
  gsp_fini();
  gcrp_fini();
  gp2pp_fini();
  if (deb != NULL)
    fclose(deb);
}

/**
 * Call this function to initialize the garena library.
 *
 * @return 0 if success, -1 if the library initialization failed.
 */
int garena_init() {
  if (gsp_init() == -1) {
    return -1;
  }
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

  signal(SIGPIPE, SIG_IGN);  
  printf("Garena library initialized (version %s)\n", VERSION);
  return 0;
}
