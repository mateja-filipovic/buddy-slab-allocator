#include <string.h>

#include "cache.h"
#include "check.h"

int main(void) {
	void* space = malloc(1000 * BLOCK_SIZE);
	kmem_init(space, 1000);

	kmem_cache_t* c = kmem_cache_create("info", 64, NULL, NULL);
	int half = c->objs_per_slab / 2;
	int i;
	for (i = 0; i < half; i++)
		CHECK(kmem_cache_alloc(c) != NULL);

	char path[] = "info_output.txt";
	CHECK(freopen(path, "w", stdout) != NULL);
	kmem_cache_info(c);
	kmem_cache_error(c);
	fflush(stdout);
	CHECK(freopen("/dev/tty", "w", stdout) != NULL || freopen("CON", "w", stdout) != NULL);

	FILE* f = fopen(path, "r");
	CHECK(f != NULL);
	char out[1024] = { 0 };
	fread(out, 1, sizeof out - 1, f);
	fclose(f);
	remove(path);

	int expected = 100 * half / c->objs_per_slab;
	char line[64];
	snprintf(line, sizeof line, "Occupancy: %d%%\n", expected);
	CHECK(strstr(out, "Cache name: info\n") != NULL);
	CHECK(strstr(out, "Object size: 64\n") != NULL);
	CHECK(strstr(out, line) != NULL);
	CHECK(strstr(out, "Cache info: no errors\n") != NULL);

	printf("PASS\n");
	return 0;
}
