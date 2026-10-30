#include "cache.h"
#include "check.h"

static int ctor_calls, dtor_calls;
static void ctor(void* p) { ctor_calls++; *(int*)p = 7; }
static void dtor(void* p) { if (*(int*)p == 7) dtor_calls++; }

int main(void) {
	void* space = malloc(1000 * BLOCK_SIZE);
	kmem_init(space, 1000);

	kmem_cache_t* c = kmem_cache_create("obj", 100, ctor, dtor);
	int per_slab = c->objs_per_slab;
	CHECK(per_slab > 1);

	void* a = kmem_cache_alloc(c);
	CHECK(a != NULL);
	CHECK(ctor_calls == per_slab);

	kmem_cache_destroy(c);
	CHECK(dtor_calls == per_slab);

	printf("PASS\n");
	return 0;
}
