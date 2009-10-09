#ifndef GARENA_GARENA_H
#define GARENA_GARENA_H 1
#include <stdint.h>
#include <garena/config.h>
/*
 * Garena uses little-endian words in packets sent to the network.
 * Therefore, if this CPU is little-endian ghtons/ghtonl does nothing.
 * Otherwise, it swaps the words from big endian to little endian.
 */
#ifdef WORDS_BIGENDIAN
#define ghtons(x) bswap_16(x)
#define ghtonl(x) bswap_32(x)
#else
#define ghtons(x) (x)
#define ghtonl(x) (x)
#endif

typedef uint32_t gtime_t;

#define GARENA_NETWORK "192.168.29.0"
#define FWD_NETWORK "192.168.28.0"
int garena_init(void);
void garena_fini(void);

#define DEBUG_LOG "garena.log"
extern FILE *deb;


#endif
