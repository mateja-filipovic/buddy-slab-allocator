#include "cache.h"
#include "check.h"

static int reference(unsigned long n) {
	int p = 0;
	unsigned long v = 1;
	while (v < n) { v *= 2; p++; }
	return p;
}

int main(void) {
	unsigned long n;
	for (n = 0; n <= (1UL << 20); n++)
		CHECK(_ceil_log2(n) == reference(n));

	void* space = malloc(1000 * BLOCK_SIZE);
	kmem_init(space, 1000);
	CHECK(BUDDY->max_order == 10);

	void* p = kmalloc(1024);
	CHECK(p != NULL);
	CHECK(slab_of(p)->cache->obj_size == 1024);

	void* q = kmalloc(1025);
	CHECK(slab_of(q)->cache->obj_size == 2048);

	void* r = kmalloc(0);
	CHECK(slab_of(r)->cache->obj_size == 32);

	CHECK(kmalloc((1 << 17) + 1) == NULL);

	printf("PASS\n");
	return 0;
}
