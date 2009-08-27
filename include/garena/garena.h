#ifndef GARENA_GARENA_H
#define GARENA_GARENA_H 1
#include <stdint.h>

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


#define GARENA_NETWORK "192.168.29.0"
int garena_init(void);

#define DEBUG_LOG "/tmp/garena.log"
extern FILE *deb;


#endif
