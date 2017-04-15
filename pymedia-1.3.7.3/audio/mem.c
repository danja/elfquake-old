/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "mem.h"

/* allocation of static arrays - do not use for normal allocation */
static unsigned int last_static = 0;
static char*** array_static = NULL;
static const unsigned int grow_static = 64; // ^2
static int mem_allocated= 0;

void *av_mallocz(unsigned int size)
{
    void *ptr;
    
    if(size == 0) fprintf(stderr, "Warning, allocating 0 bytes\n");
    
    ptr = av_malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}

void *__av_mallocz_static(void** location, unsigned int size)
{
    int l = (last_static + grow_static) & ~(grow_static - 1);
    void *ptr = av_mallocz(size);
    if (!ptr)
	return NULL;

    if (location)
    {
	if (l > last_static)
	    array_static = realloc(array_static, l);
	array_static[last_static++] = (char**) location;
	*location = ptr;
    }
    return ptr;
}
/* free all static arrays and reset pointers to 0 */
void av_free_static()
{
    if (array_static)
    {
	unsigned i;
	for (i = 0; i < last_static; i++)
	{
	    free(*array_static[i]);
            *array_static[i] = NULL;
	}
	free(array_static);
	array_static = 0;
    }
    last_static = 0;
}

/* cannot call it directly because of 'void **' casting is not automatic */
void __av_freep(void **ptr)
{
    av_free(*ptr);
    *ptr = NULL;
}


/** 
 * Memory allocation of size byte with alignment suitable for all
 * memory accesses (including vectors if available on the
 * CPU). av_malloc(0) must return a non NULL pointer.
 */
void *av_malloc(unsigned int size)
{
    void *ptr;
    
#ifdef MEMALIGN_HACK
    int diff;
    ptr = malloc(size+16+1);
    diff= ((-(int)ptr - 1)&15) + 1;
    ptr += diff;
    ((char*)ptr)[-1]= diff;
#elif defined (HAVE_MEMALIGN) 
    ptr = memalign(16,size);
    /* Why 64? 
       Indeed, we should align it:
         on 4 for 386
         on 16 for 486
	 on 32 for 586, PPro - k6-III
	 on 64 for K7 (maybe for P3 too).
       Because L1 and L2 caches are aligned on those values.
       But I don't want to code such logic here!
     */
     /* Why 16?
        because some cpus need alignment, for example SSE2 on P4, & most RISC cpus
        it will just trigger an exception and the unaligned load will be done in the
        exception handler or it will just segfault (SSE2 on P4)
        Why not larger? because i didnt see a difference in benchmarks ...
     */
     /* benchmarks with p3
        memalign(64)+1		3071,3051,3032
        memalign(64)+2		3051,3032,3041
        memalign(64)+4		2911,2896,2915
        memalign(64)+8		2545,2554,2550
        memalign(64)+16		2543,2572,2563
        memalign(64)+32		2546,2545,2571
        memalign(64)+64		2570,2533,2558
        
        btw, malloc seems to do 8 byte alignment by default here
     */
#else
    ptr = malloc(size);
#endif
    return ptr;
}

/**
 * av_realloc semantics (same as glibc): if ptr is NULL and size > 0,
 * identical to malloc(size). If size is zero, it is identical to
 * free(ptr) and NULL is returned.  
 */
void *av_realloc(void *ptr, unsigned int size)
{
#ifdef MEMALIGN_HACK
    //FIXME this isnt aligned correctly though it probably isnt needed
    int diff= ptr ? ((char*)ptr)[-1] : 0;
    return realloc(ptr - diff, size + diff) + diff;
#else
    return realloc(ptr, size);
#endif
}

/* NOTE: ptr = NULL is explicetly allowed */
void av_free(void *ptr)
{
    /* XXX: this test should not be needed on most libcs */
    if (ptr)
#ifdef MEMALIGN_HACK
        free(ptr - ((char*)ptr)[-1]);
#else
        free(ptr);
#endif
}
 
/**
 * realloc which does nothing if the block is large enough
 */
void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    if(min_size < *size) 
        return ptr;
    
    *size= 17*min_size/16 + 32;

    return av_realloc(ptr, *size);
}
