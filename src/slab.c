#include <string.h>
#include <stdio.h>

#include "../inc/slab.h"
#include "../inc/buddy.h"
#include "../inc/cache.h"

#ifdef _WIN32
#include <windows.h>
static HANDLE mtx;
static int mutex_init(void) { mtx = CreateSemaphore(NULL, 1, 1, NULL); return mtx != NULL; }
void mutex_wait() { WaitForSingleObject(mtx, INFINITE); }
void mutex_signal() { ReleaseSemaphore(mtx, 1, NULL); }
#else
#include <pthread.h>
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int mutex_init(void) { return 1; }
void mutex_wait() { pthread_mutex_lock(&mtx); }
void mutex_signal() { pthread_mutex_unlock(&mtx); }
#endif

#define SIZE_CACHE_MIN 5
#define SIZE_CACHE_MAX 17

//pointer to the first cache
kmem_cache_s* cache_list_head = NULL;
//small memory buffers, index 0 holds size 2^SIZE_CACHE_MIN
static kmem_cache_s* size_caches[SIZE_CACHE_MAX - SIZE_CACHE_MIN + 1];


void kmem_init(void* space, int block_num){

	if (!mutex_init()) {
		printf("Error: Cannot allocate a mutex\n");
		return;
	}

	//initalize the buddy allocator
	buddy_init(space, block_num);
	cache_list_head = NULL;

	if (slab_table_init() != 0) {
		printf("Error: Not enough space for the slab table\n");
		return;
	}

	//initialize small memory buffers
	int i;
	for (i = SIZE_CACHE_MIN; i <= SIZE_CACHE_MAX; i++) {
		char buffer_name[30];
		snprintf(buffer_name, sizeof buffer_name, "size-2^%d", i);
		kmem_cache_s* c = kmem_cache_create(buffer_name, (size_t)1 << i, NULL, NULL);
		if (c != NULL)
			c->is_size_cache = 1;
		size_caches[i - SIZE_CACHE_MIN] = c;
	}
}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void(*dtor)(void*)){
	mutex_wait();
	void* ret = _kmem_cache_create(name, size, ctor, dtor);
	mutex_signal();
	return ret;
}

kmem_cache_t* _kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void (*dtor)(void*)) {
	//alocate a new cache manager
	kmem_cache_s* cache = (kmem_cache_s*)buddy_allocate(1);

	//error checking
	if (cache == NULL) {
		printf("Cache error: Unable to alocate another cache manager!\n");
		return cache;
	}

	//calculate how many blocks one slab takes up
	int block_num = (10 * size) / BLOCK_SIZE;
	if (10 *size % BLOCK_SIZE != 0)
		block_num++;
	block_num = _get_blk_size(_ceil_log2(block_num));

	int obj_offset, capacity;
	slab_layout((int)size, block_num, &obj_offset, &capacity);
	
	//initializing the members
	cache->empty_slabs = NULL;
	cache->partial_slabs = NULL;
	cache->full_slabs = NULL;

	cache->count_empty = 0;
	cache->count_partial = 0;
	cache->count_full = 0;

	cache->obj_size = (int)size;

	cache->num_of_allocated_objs = 0;
	cache->num_of_blocks = 1;
	cache->objs_per_slab = capacity;
	cache->blocks_per_slab = block_num;

	cache->num_of_errors = 0;

	cache->wasted = block_num * BLOCK_SIZE - obj_offset - capacity * (int)size;
	cache->offset = 0;

	cache->growing = 0;
	cache->is_size_cache = 0;

	snprintf(cache->name, sizeof cache->name, "%s", name);

	cache->ctor = ctor;
	cache->dtor = dtor;

	//update the cache list
	cache->next = cache_list_head;
	cache_list_head = cache;

	return cache;
}

int kmem_cache_shrink(kmem_cache_t* cachep){
	mutex_wait();
	int ret = _kmem_cache_shrink(cachep);
	mutex_signal();
	return ret;
}

int _kmem_cache_shrink(kmem_cache_t* cachep) {

	int num_freed = 0;

	//check if the pointer is valid
	if (!is_cache_valid(cachep)) {
		printf("No such cache exists!\n");
		return num_freed;
	}

	if (cachep->growing) {
		cachep->growing = 0;
		return num_freed;
	}

	//find and deallocate all empty slabs
	slab_s* tmp = cachep->empty_slabs, * helper = NULL;
	while (tmp != NULL) {
		helper = tmp->next;
		cachep->num_of_blocks -= cachep->blocks_per_slab;
		num_freed += cachep->blocks_per_slab;
		deallocate_a_slab(tmp);
		tmp = helper;
	}

	//reset the empty slab list
	cachep->empty_slabs = NULL;
	cachep->count_empty = 0;

	return num_freed;
}

void* kmem_cache_alloc(kmem_cache_t* cachep){
	mutex_wait();
	void* ret = _kmem_cache_alloc(cachep);
	mutex_signal();
	return ret;
}

void* _kmem_cache_alloc(kmem_cache_t* cachep) {
	//check if the pointer is valid
	if (!is_cache_valid(cachep)) {
		printf("Cache error: No such cache exists");
		return NULL;
	}

	slab_s* sl;
	//try to allocate an object in partialy filled slabs
	if (cachep->partial_slabs != NULL) {
		sl = cachep->partial_slabs;
	}
	//try to allocate an object in empty slabs
	else if (cachep->empty_slabs != NULL) {
		sl = cachep->empty_slabs;
		slab_list_remove(&cachep->empty_slabs, sl);
		slab_list_push(&cachep->partial_slabs, sl);
		cachep->count_empty--;
		cachep->count_partial++;
	}
	else { //must allocate a new slab
		sl = allocate_a_slab(cachep, cachep->offset);
		//error checking!
		if (sl == NULL) {
			printf("Cache error: Can't allocate a slab\n");
			cachep->num_of_errors++;
			return NULL;
		}
		cachep->offset += CACHE_L1_LINE_SIZE;
		if (cachep->offset > cachep->wasted)
			cachep->offset = 0;
		cachep->num_of_blocks += cachep->blocks_per_slab;
		cachep->growing = 1;

		slab_list_push(&cachep->partial_slabs, sl);
		cachep->count_partial++;
	}

	void* ret = allocate_obj_from_slab(sl);

	//move the slab to full slab list if needed
	if (sl->state == 2) {
		slab_list_remove(&cachep->partial_slabs, sl);
		slab_list_push(&cachep->full_slabs, sl);
		cachep->count_partial--;
		cachep->count_full++;
	}

	cachep->num_of_allocated_objs++;
	return ret;
}

void kmem_cache_free(kmem_cache_t* cachep, void* objp) {
	mutex_wait();
	_kmem_cache_free(cachep, objp);
	mutex_signal();
}

int _kmem_cache_free(kmem_cache_t* cachep, const void* objp) {
	//is it a valid cache pointer
	if (!is_cache_valid(cachep)) {
		printf("Cache error: Unable to dealloc object, no such cache exists\n");
		return -1;
	}

	slab_s* sl = slab_of(objp);
	if (sl == NULL || sl->cache != cachep) {
		printf("Cache error: No such object exists!\n");
		cachep->num_of_errors++;
		return -1;
	}

	int was_full = (sl->state == 2);
	if (dealloc_obj_from_slab(sl, objp) != 0) {
		printf("Cache error: No such object exists!\n");
		cachep->num_of_errors++;
		return -1;
	}
	cachep->num_of_allocated_objs--;

	//move the slab between lists if needed
	if (was_full) {
		slab_list_remove(&cachep->full_slabs, sl);
		cachep->count_full--;
	}
	else {
		slab_list_remove(&cachep->partial_slabs, sl);
		cachep->count_partial--;
	}
	if (sl->state == 0) {
		slab_list_push(&cachep->empty_slabs, sl);
		cachep->count_empty++;
	}
	else {
		slab_list_push(&cachep->partial_slabs, sl);
		cachep->count_partial++;
	}

	return 0;
}

void* kmalloc(size_t size){
	mutex_wait();
	void* ret = _kmalloc(size);
	mutex_signal();
	return ret;
}

void* _kmalloc(size_t size) {

	//max value is 2^17
	if (size > ((size_t)1 << SIZE_CACHE_MAX)) {
		printf("Cache error: No buffer is that big!\n");
		return NULL;
	}

	int power = _ceil_log2(size);
	//min value is 2^5
	if (power < SIZE_CACHE_MIN)
		power = SIZE_CACHE_MIN;

	kmem_cache_s* cp = size_caches[power - SIZE_CACHE_MIN];
	if (cp == NULL) {
		printf("Cache error: Could not find buffer!\n");
		return NULL;
	}

	//no space to allocate another slab!
	void* ret = _kmem_cache_alloc(cp);
	if (ret == NULL)
		printf("Cache error: Can't allocate another slab for kmalloc!\n");

	return ret;
}

void kfree(const void* objp){
	mutex_wait();
	_kfree(objp);
	mutex_signal();
}

void _kfree(const void* objp) {
	slab_s* sl = slab_of(objp);
	if (sl == NULL || !sl->cache->is_size_cache) {
		printf("Cache error: no such object allocated in buffers!\n");
		return;
	}
	_kmem_cache_free(sl->cache, objp);
}

void kmem_cache_destroy(kmem_cache_t* cachep){
	mutex_wait();
	_kmem_cache_destroy(cachep);
	mutex_signal();
}

void _kmem_cache_destroy(kmem_cache_t* cachep) {
	if (!is_cache_valid(cachep)) {
		printf("Cache error: No such cache exists to be destroyed\n");
		return;
	}
	//first deallocate all of the slabs
	slab_s* tmp, * helper;

	//dealloc empty slabs
	tmp = cachep->empty_slabs, helper = NULL;
	while (tmp != NULL) {
		helper = tmp->next;
		deallocate_a_slab(tmp);
		tmp = helper;
	}
	//deallocate partial slabs
	tmp = cachep->partial_slabs, helper = NULL;
	while (tmp != NULL) {
		helper = tmp->next;
		deallocate_a_slab(tmp);
		tmp = helper;
	}

	//deallocate full slabs
	tmp = cachep->full_slabs, helper = NULL;
	while (tmp != NULL) {
		helper = tmp->next;
		deallocate_a_slab(tmp);
		tmp = helper;
	}

	//update the cache list
	if (cachep == cache_list_head)
		cache_list_head = cachep->next;
	else {
		kmem_cache_s* current = cache_list_head, * helper = NULL;
		while (current != cachep) {
			helper = current;
			current = current->next;
		}
		if(helper != NULL) //handle nullptr exception
			helper->next = current->next;
	}

	//deallocate the cache itself
	buddy_deallocate(cachep, 1);
}

void kmem_cache_info(kmem_cache_t* cachep){
	mutex_wait();
	_kmem_cache_info(cachep);
	mutex_signal();
}

void _kmem_cache_info(kmem_cache_t* cachep) {
	if (!is_cache_valid(cachep)) {
		printf("Cache error: Cannot print, such cache does not exist!\n");
		return;
	}
	print_cache(cachep);
}

void print_cache(kmem_cache_s* c) {
	printf("Cache name: %s\n", c->name);
	printf("Object size: %d\n", c->obj_size);
	printf("Blocks occcupied (including slabs): %d\n", c->num_of_blocks);
	printf("This cache has %d slabs right now\n", c->count_empty + c->count_full + c->count_partial);
	printf("Num of objects per slab: %d\n", c->objs_per_slab);
	printf("Num of objects allocated: %d\n", c->num_of_allocated_objs);
	int max_objs = (c->count_empty + c->count_partial + c->count_full) * c->objs_per_slab;
	if (c->num_of_allocated_objs != 0)
		printf("Occupancy so far: %.2f\n", (double)c->num_of_allocated_objs / max_objs);
	printf("\n");
}


int kmem_cache_error(kmem_cache_t* cachep){
	mutex_wait();

	if (!is_cache_valid(cachep)) {
		printf("Cache error: Cannot print, such cache does not exist!\n");
		mutex_signal();
		return -1;
	}

	printf("Errors found so far for cache named %s: %d\n", cachep->name, cachep->num_of_errors);
	mutex_signal();
	return cachep->num_of_errors;
}

//helper function to see if the cachep is valid
int is_cache_valid(void* cachep){
	kmem_cache_s* tmp = cache_list_head;
	while (tmp != NULL) {
		if (tmp == cachep)
			return 1;
		tmp = tmp->next;
	}
	return 0;
}
