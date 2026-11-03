#include "cache.h"
#include "check.h"

int main(void) {
	void* space = malloc(1000 * BLOCK_SIZE);
	kmem_init(space, 1000);

	kmem_cache_t* c = kmem_cache_create("obj", 100, NULL, NULL);
	void* a = kmem_cache_alloc(c);
	void* b = kmem_cache_alloc(c);
	CHECK(a != NULL && b != NULL && a != b);

	kmem_cache_free(c, a);
	kmem_cache_free(c, a);
	CHECK(kmem_cache_error(c) == KMEM_ERR_DOUBLE_FREE);
	CHECK(c->num_of_allocated_objs == 1);
	CHECK(c->count_partial == 1 && c->count_empty == 0);

	CHECK(kmem_cache_alloc(c) == a);

	printf("PASS\n");
	return 0;
}
