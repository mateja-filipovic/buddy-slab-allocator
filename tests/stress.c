#include <string.h>

#include "cache.h"
#include "check.h"

#define SLOTS 400
#define OPS 20000

typedef struct {
	void* ptr;
	size_t size;
	int cache;
} slot_s;

static unsigned long seed = 12345;
static unsigned long next_rand(void) {
	seed = seed * 1103515245UL + 12345UL;
	return (seed >> 16) & 0x7fff;
}

static int verify(slot_s* s, int tag) {
	size_t i;
	for (i = 0; i < s->size; i++)
		if (((unsigned char*)s->ptr)[i] != (unsigned char)tag)
			return 0;
	return 1;
}

int main(void) {
	void* space = malloc(4000 * BLOCK_SIZE);
	kmem_init(space, 4000);

	kmem_cache_t* caches[3];
	size_t sizes[3] = { 24, 100, 700 };
	int c;
	for (c = 0; c < 3; c++)
		caches[c] = kmem_cache_create("stress", sizes[c], NULL, NULL);

	slot_s slots[SLOTS];
	memset(slots, 0, sizeof slots);

	int op;
	for (op = 0; op < OPS; op++) {
		int i = next_rand() % SLOTS;
		int tag = i + 1;
		if (slots[i].ptr != NULL) {
			CHECK(verify(&slots[i], tag));
			if (slots[i].cache < 0)
				kfree(slots[i].ptr);
			else
				kmem_cache_free(caches[slots[i].cache], slots[i].ptr);
			slots[i].ptr = NULL;
		}
		else {
			int which = next_rand() % 4;
			if (which == 3) {
				slots[i].size = 1 + next_rand() % 5000;
				slots[i].cache = -1;
				slots[i].ptr = kmalloc(slots[i].size);
			}
			else {
				slots[i].size = sizes[which];
				slots[i].cache = which;
				slots[i].ptr = kmem_cache_alloc(caches[which]);
			}
			CHECK(slots[i].ptr != NULL);
			memset(slots[i].ptr, tag, slots[i].size);
		}
	}

	int i;
	for (i = 0; i < SLOTS; i++) {
		if (slots[i].ptr == NULL)
			continue;
		CHECK(verify(&slots[i], i + 1));
		if (slots[i].cache < 0)
			kfree(slots[i].ptr);
		else
			kmem_cache_free(caches[slots[i].cache], slots[i].ptr);
	}

	for (c = 0; c < 3; c++) {
		CHECK(kmem_cache_error(caches[c]) == 0);
		CHECK(caches[c]->num_of_allocated_objs == 0);
		kmem_cache_shrink(caches[c]);
		kmem_cache_shrink(caches[c]);
		CHECK(caches[c]->num_of_blocks == 1);
		CHECK(caches[c]->count_empty == 0 && caches[c]->count_partial == 0 && caches[c]->count_full == 0);
		kmem_cache_destroy(caches[c]);
	}

	printf("PASS\n");
	return 0;
}
