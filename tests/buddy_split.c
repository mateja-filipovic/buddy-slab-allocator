#include <string.h>

#include "buddy.h"
#include "check.h"

int main(void) {
	int n = 33;
	void* space = malloc(n * BLOCK_SIZE);
	buddy_init(space, n);

	void* b = buddy_allocate(1);
	void* d = buddy_allocate(2);
	memset(d, 0xAB, 2 * BLOCK_SIZE);
	buddy_deallocate(d, 2);
	buddy_deallocate(b, 1);

	buddy_allocate(1);
	buddy_allocate(1);
	buddy_allocate(1);
	buddy_allocate(1);
	CHECK(BUDDY->pool[0] == NULL);

	printf("PASS\n");
	return 0;
}
