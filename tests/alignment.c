#include <stdint.h>

#include "cache.h"
#include "check.h"

int main(void) {
	void* space = malloc(1000 * BLOCK_SIZE);
	kmem_init(space, 1000);

	kmem_cache_t* tiny = kmem_cache_create("tiny", 7, NULL, NULL);
	CHECK(tiny->obj_size == 7);
	CHECK(tiny->stride == 8);
	char* a = kmem_cache_alloc(tiny);
	char* b = kmem_cache_alloc(tiny);
	CHECK(b - a == 8);
	CHECK((uintptr_t)a % OBJ_ALIGN == 0 && (uintptr_t)b % OBJ_ALIGN == 0);

	kmem_cache_t* odd = kmem_cache_create("odd", 100, NULL, NULL);
	CHECK(odd->obj_size == 100);
	CHECK(odd->stride == 104);
	char* c = kmem_cache_alloc(odd);
	CHECK((uintptr_t)c % OBJ_ALIGN == 0);

	kmem_cache_free(tiny, a);
	kmem_cache_free(tiny, b);
	kmem_cache_free(odd, c);
	CHECK(kmem_cache_error(tiny) == KMEM_ERR_NONE);
	CHECK(kmem_cache_error(odd) == KMEM_ERR_NONE);

	printf("PASS\n");
	return 0;
}
