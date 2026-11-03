#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

#define NUM_OF_BLOCKS 1000
#define NUM_OF_OBJS 50
#define DEALLOC_LOW 30 //used for deallocation testing

int main() {

	// INITIALIZE THE ALLOCATOR
	void* address_space = malloc(NUM_OF_BLOCKS * BLOCK_SIZE);
	kmem_init(address_space, NUM_OF_BLOCKS);

	// CREATING A CACHE
	kmem_cache_t* cache_pcb = kmem_cache_create("PCB", 100, NULL, NULL);

	// ALLOCATING OBJECTS
	void* pcbs[NUM_OF_OBJS];
	for (int i = 0; i < NUM_OF_OBJS; i++)
		pcbs[i] = kmem_cache_alloc(cache_pcb);

	// PRINTING CACHE INFO
	kmem_cache_info(cache_pcb);

	// DEALLOCATING SOME OBJECTS
	for (int i = DEALLOC_LOW; i < NUM_OF_OBJS; i++)
		kmem_cache_free(cache_pcb, pcbs[i]);

	// PRINTING INFO AFTER DEALLOCATING
	kmem_cache_info(cache_pcb);

	// SHRINKING THE CACHE
	printf("Calling cache shrink right after the cache grew: blocks freed = %d\n", kmem_cache_shrink(cache_pcb));
	printf("Calling cache shrink again: blocks freed = %d\n\n", kmem_cache_shrink(cache_pcb));
	kmem_cache_info(cache_pcb);

	// CHECKING FOR ERRORS
	kmem_cache_error(cache_pcb);

	// DESTROYING THE CACHE
	kmem_cache_destroy(cache_pcb);

	// TEST END, FREEING THE MEMORY
	free(address_space);

	return 0;
}
