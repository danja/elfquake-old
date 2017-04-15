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
	if (l > (int)last_static)
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

 
/* you can redefine av_malloc and av_free in your project to use your
   memory allocator. You do not need to suppress this file because the
   linker will do it automatically */

static void *xvid_malloc(size_t size, uint8_t alignment)
{
	uint8_t *mem_ptr;
  
	if(!alignment)
	{

		/* We have not to satisfy any alignment */
		if((mem_ptr = (uint8_t *) malloc(size + 1)) != NULL)
		{

			/* Store (mem_ptr - "real allocated memory") in *(mem_ptr-1) */
			*mem_ptr = 0;

			/* Return the mem_ptr pointer */
			return (void *) mem_ptr++;

		}

	}
	else
	{
		uint8_t *tmp;
	
		/*
		 * Allocate the required size memory + alignment so we
		 * can realign the data if necessary
		 */

		if((tmp = (uint8_t *) malloc(size + alignment)) != NULL)
		{

			/* Align the tmp pointer */
			mem_ptr = (uint8_t *)((uint32_t)(tmp + alignment - 1)&(~(uint32_t)(alignment - 1)));

			/*
			 * Special case where malloc have already satisfied the alignment
			 * We must add alignment to mem_ptr because we must store
			 * (mem_ptr - tmp) in *(mem_ptr-1)
			 * If we do not add alignment to mem_ptr then *(mem_ptr-1) points
			 * to a forbidden memory space
			 */
			if(mem_ptr == tmp) mem_ptr += alignment;

			/*
			 * (mem_ptr - tmp) is stored in *(mem_ptr-1) so we are able to retrieve
			 * the real malloc block allocated and free it in xvid_free
			 */
			*(mem_ptr - 1) = (uint8_t)(mem_ptr - tmp);

			/* Return the aligned pointer */
			return (void *) mem_ptr;

		}
	}

	return NULL;

}

/* memory alloc */
void *av_malloc(unsigned int size)
{
    void *ptr = xvid_malloc(size,64);
//mem_allocated+= size;
//printf( "alloc: %d\n", size );
    if (!ptr) return NULL;
    memset(ptr, 0, size);
    return ptr;
}

/* NOTE: ptr = NULL is explicetly allowed */
void av_free(void *ptr)
{
    /* XXX: this test should not be needed on most libcs */
    if (ptr)
		{
//mem_allocated-= *(int*)( (uint8_t*)ptr - *((uint8_t*)ptr - 1)- 16 )- 64;
//printf( "free: %d\n", *(int*)( (uint8_t*)ptr - *((uint8_t*)ptr - 1)- 16 )- 64 );
         free((uint8_t*)ptr - *((uint8_t*)ptr - 1));
		}

}


/**
 * av_realloc semantics (same as glibc): if ptr is NULL and size > 0,
 * identical to malloc(size). If size is zero, it is identical to
 * free(ptr) and NULL is returned.  
 */
void *av_realloc(void *ptr, unsigned int size)
{
	if (!ptr)
		return malloc(size);
	else
		return realloc(ptr, size);
}
 
/**
 * realloc which does nothing if the block is large enough
 */
void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    if(min_size < *size) 
        return ptr;
    
    *size= min_size + 10*1024;

    return av_realloc(ptr, *size);
}

 
