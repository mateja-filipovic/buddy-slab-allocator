#include "cache.h"
#include "check.h"

int main(void) {
	void* space = malloc(1000 * BLOCK_SIZE);
	kmem_init(space, 1000);

	kmem_cache_t* c = kmem_cache_create("obj", 100, NULL, NULL);
	int n = 2 * c->objs_per_slab;
	void** objs = malloc(n * sizeof(void*));
	int i;
	for (i = 0; i < n; i++) {
		objs[i] = kmem_cache_alloc(c);
		CHECK(objs[i] != NULL);
	}
	CHECK(c->count_full == 2);

	for (i = 0; i < n; i++)
		kmem_cache_free(c, objs[i]);
	CHECK(c->count_empty == 2);

	CHECK(kmem_cache_shrink(c) == 0);
	CHECK(c->count_empty == 2);

	CHECK(kmem_cache_shrink(c) == 2 * c->blocks_per_slab);
	CHECK(c->count_empty == 0);
	CHECK(c->num_of_blocks == 1);

	void* a = kmem_cache_alloc(c);
	CHECK(a != NULL);
	CHECK(kmem_cache_error(c) == 0);

	printf("PASS\n");
	return 0;
}
