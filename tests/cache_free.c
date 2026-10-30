#include "cache.h"
#include "check.h"

int main(void) {
	void* space = malloc(1000 * BLOCK_SIZE);
	kmem_init(space, 1000);

	kmem_cache_t* a = kmem_cache_create("a", 100, NULL, NULL);
	kmem_cache_t* b = kmem_cache_create("b", 200, NULL, NULL);

	void* obj = kmem_cache_alloc(a);
	kmem_cache_free(b, obj);
	CHECK(kmem_cache_error(b) == 1);
	CHECK(a->num_of_allocated_objs == 1);

	kfree(obj);
	CHECK(a->num_of_allocated_objs == 1);

	kmem_cache_free(a, (char*)obj + 1);
	CHECK(kmem_cache_error(a) == 1);
	CHECK(a->num_of_allocated_objs == 1);

	void* buf = kmalloc(300);
	kmem_cache_t* size_cache = slab_of(buf)->cache;
	CHECK(size_cache->is_size_cache);
	CHECK(size_cache->num_of_allocated_objs == 1);
	kfree(buf);
	CHECK(size_cache->num_of_allocated_objs == 0);

	int n = a->objs_per_slab + 1;
	void** objs = malloc(n * sizeof(void*));
	objs[0] = obj;
	int i;
	for (i = 1; i < n; i++)
		objs[i] = kmem_cache_alloc(a);
	CHECK(a->count_full == 1 && a->count_partial == 1);

	kmem_cache_free(a, objs[0]);
	CHECK(a->count_full == 0 && a->count_partial == 2);
	CHECK(kmem_cache_alloc(a) == objs[0]);
	CHECK(a->count_full == 1 && a->count_partial == 1);

	printf("PASS\n");
	return 0;
}
