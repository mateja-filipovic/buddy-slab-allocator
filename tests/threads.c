#include <string.h>

#include "cache.h"
#include "check.h"

#define THREADS 4
#define ITERS 5000
#define LIVE 64

#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
static void start(thread_t* t, void (*fn)(void*), void* arg) { *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL); }
static void join(thread_t t) { WaitForSingleObject(t, INFINITE); }
#else
#include <pthread.h>
typedef pthread_t thread_t;
static void start(thread_t* t, void (*fn)(void*), void* arg) { pthread_create(t, NULL, (void* (*)(void*))fn, arg); }
static void join(thread_t t) { pthread_join(t, NULL); }
#endif

typedef struct {
	int id;
	kmem_cache_t* cache;
	int failures;
} worker_s;

static unsigned long next_rand(unsigned long* seed) {
	*seed = *seed * 1103515245UL + 12345UL;
	return (*seed >> 16) & 0x7fff;
}

static void worker(void* arg) {
	worker_s* w = arg;
	unsigned long seed = 1000 + w->id;
	void* live[LIVE] = { 0 };
	int from_kmalloc[LIVE] = { 0 };
	int i;
	for (i = 0; i < ITERS; i++) {
		int slot = next_rand(&seed) % LIVE;
		unsigned char tag = (unsigned char)(w->id * 16 + slot % 16 + 1);
		if (live[slot] != NULL) {
			if (((unsigned char*)live[slot])[0] != tag || ((unsigned char*)live[slot])[31] != tag)
				w->failures++;
			if (from_kmalloc[slot])
				kfree(live[slot]);
			else
				kmem_cache_free(w->cache, live[slot]);
			live[slot] = NULL;
		}
		else {
			from_kmalloc[slot] = next_rand(&seed) % 2;
			live[slot] = from_kmalloc[slot] ? kmalloc(32 + next_rand(&seed) % 200) : kmem_cache_alloc(w->cache);
			if (live[slot] == NULL) { w->failures++; continue; }
			memset(live[slot], tag, 32);
		}
	}
	for (i = 0; i < LIVE; i++) {
		if (live[i] == NULL)
			continue;
		if (from_kmalloc[i])
			kfree(live[i]);
		else
			kmem_cache_free(w->cache, live[i]);
	}
}

int main(void) {
	void* space = malloc(4000 * BLOCK_SIZE);
	kmem_init(space, 4000);

	worker_s workers[THREADS];
	thread_t threads[THREADS];
	int i;
	for (i = 0; i < THREADS; i++) {
		workers[i].id = i;
		workers[i].cache = kmem_cache_create("worker", 48, NULL, NULL);
		workers[i].failures = 0;
		start(&threads[i], worker, &workers[i]);
	}
	for (i = 0; i < THREADS; i++)
		join(threads[i]);

	for (i = 0; i < THREADS; i++) {
		CHECK(workers[i].failures == 0);
		CHECK(kmem_cache_error(workers[i].cache) == KMEM_ERR_NONE);
		CHECK(workers[i].cache->num_of_allocated_objs == 0);
		kmem_cache_destroy(workers[i].cache);
	}

	void* probe = kmalloc(100);
	kmem_cache_t* size_cache = slab_of(probe)->cache;
	kfree(probe);
	CHECK(size_cache->num_of_allocated_objs == 0);
	CHECK(kmem_cache_error(size_cache) == KMEM_ERR_NONE);

	printf("PASS\n");
	return 0;
}
