#include "cache.h"
#include "check.h"

int main(void) {
	void* space = malloc(40 * BLOCK_SIZE);
	kmem_init(space, 40);

	kmem_cache_t* big = kmem_cache_create("big", 100000, NULL, NULL);
	CHECK(big != NULL);
	CHECK(kmem_cache_alloc(big) == NULL);
	CHECK(kmem_cache_error(big) == KMEM_ERR_NO_MEMORY);
	CHECK(big->num_of_blocks == 1);

	kmem_cache_t* small = kmem_cache_create("small", 100, NULL, NULL);
	void* a = kmem_cache_alloc(small);
	CHECK(a != NULL);
	kmem_cache_free(small, a);
	CHECK(kmem_cache_error(small) == 0);

	kmem_cache_destroy(small);
	kmem_cache_destroy(big);

	printf("PASS\n");
	return 0;
}
