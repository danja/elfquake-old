

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inttypes.h"

#ifndef LIBFORMAT_MEM_H
#define LIBFORMAT_MEM_H


/* memory */
void *av_malloc(unsigned int size);
void *av_mallocz(unsigned int size);
void av_free(void *ptr);
void __av_freep(void **ptr);
#define av_freep(p) __av_freep((void **)(p))
/* for static data only */
/* call av_free_static to release all staticaly allocated tables */
void av_free_static(void);
void *__av_mallocz_static(void** location, unsigned int size);
#define av_mallocz_static(p, s) __av_mallocz_static((void **)(p), s)
void *av_realloc(void *ptr, unsigned int size);

#endif
