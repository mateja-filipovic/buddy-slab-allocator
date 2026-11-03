#include "kmem_port.h"
#include "slab.h"
#include "buddy.h"
#include "cache.h"

#define SIZE_CACHE_MIN 5
#define SIZE_CACHE_MAX 17

static const char* const size_cache_names[] = {
	"size-32", "size-64", "size-128", "size-256", "size-512", "size-1024", "size-2048",
	"size-4096", "size-8192", "size-16384", "size-32768", "size-65536", "size-131072"
};

static const char* const error_messages[] = {
	"no errors",
	"cannot allocate a slab, out of memory",
	"object does not belong to this cache",
	"object was already free"
};

//guards the cache list and the size cache table
static kmem_lock_t list_lock;

//lock order: list lock, then cache lock, then buddy lock
static kmem_cache_s* lock_cache(kmem_cache_t* cachep) {
	kmem_lock_acquire(&list_lock);
	if (!is_cache_valid(cachep)) {
		kmem_lock_release(&list_lock);
		printf("Cache error: No such cache exists!\n");
		return NULL;
	}
	kmem_lock_acquire(&cachep->lock);
	kmem_lock_release(&list_lock);
	return cachep;
}

static void copy_name(char* dst, const char* src, int cap) {
	int i;
	for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

//pointer to the first cache
kmem_cache_s* cache_list_head = NULL;
//small memory buffers, index 0 holds size 2^SIZE_CACHE_MIN
static kmem_cache_s* size_caches[SIZE_CACHE_MAX - SIZE_CACHE_MIN + 1];


void kmem_init(void* space, int block_num){

	kmem_lock_init(&list_lock);

	//initalize the buddy allocator
	buddy_init(space, block_num);
	cache_list_head = NULL;

	if (slab_table_init() != 0) {
		printf("Error: Not enough space for the slab table\n");
		return;
	}

	memset(size_caches, 0, sizeof size_caches);
}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void(*dtor)(void*)){
	kmem_lock_acquire(&list_lock);
	void* ret = _kmem_cache_create(name, size, ctor, dtor);
	kmem_lock_release(&list_lock);
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

	int stride = ((int)size + OBJ_ALIGN - 1) / OBJ_ALIGN * OBJ_ALIGN;

	//calculate how many blocks one slab takes up
	int block_num = (10 * stride) / BLOCK_SIZE;
	if (10 * stride % BLOCK_SIZE != 0)
		block_num++;
	block_num = _get_blk_size(_ceil_log2(block_num));

	int obj_offset, capacity;
	slab_layout(stride, block_num, &obj_offset, &capacity);
	
	//initializing the members
	cache->empty_slabs = NULL;
	cache->partial_slabs = NULL;
	cache->full_slabs = NULL;

	cache->count_empty = 0;
	cache->count_partial = 0;
	cache->count_full = 0;

	cache->obj_size = (int)size;
	cache->stride = stride;

	cache->num_of_allocated_objs = 0;
	cache->num_of_blocks = 1;
	cache->objs_per_slab = capacity;
	cache->blocks_per_slab = block_num;

	cache->last_error = KMEM_ERR_NONE;

	cache->wasted = block_num * BLOCK_SIZE - obj_offset - capacity * stride;
	cache->offset = 0;

	cache->growing = 0;
	cache->is_size_cache = 0;

	copy_name(cache->name, name, sizeof cache->name);

	cache->ctor = ctor;
	cache->dtor = dtor;

	kmem_lock_init(&cache->lock);

	//update the cache list
	cache->next = cache_list_head;
	cache_list_head = cache;

	return cache;
}

int kmem_cache_shrink(kmem_cache_t* cachep){
	if (lock_cache(cachep) == NULL)
		return 0;
	int ret = _kmem_cache_shrink(cachep);
	kmem_lock_release(&cachep->lock);
	return ret;
}

int _kmem_cache_shrink(kmem_cache_t* cachep) {

	int num_freed = 0;

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
	if (lock_cache(cachep) == NULL)
		return NULL;
	void* ret = _kmem_cache_alloc(cachep);
	kmem_lock_release(&cachep->lock);
	return ret;
}

void* _kmem_cache_alloc(kmem_cache_t* cachep) {
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
			cachep->last_error = KMEM_ERR_NO_MEMORY;
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
	if (lock_cache(cachep) == NULL)
		return;
	_kmem_cache_free(cachep, objp);
	kmem_lock_release(&cachep->lock);
}

int _kmem_cache_free(kmem_cache_t* cachep, const void* objp) {
	slab_s* sl = slab_of(objp);
	if (sl == NULL || sl->cache != cachep) {
		printf("Cache error: No such object exists!\n");
		cachep->last_error = KMEM_ERR_INVALID_OBJECT;
		return -1;
	}

	int was_full = (sl->state == 2);
	int result = dealloc_obj_from_slab(sl, objp);
	if (result == -3) {
		printf("Cache error: Object was already freed!\n");
		cachep->last_error = KMEM_ERR_DOUBLE_FREE;
		return -1;
	}
	if (result != 0) {
		printf("Cache error: No such object exists!\n");
		cachep->last_error = KMEM_ERR_INVALID_OBJECT;
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
	kmem_lock_acquire(&list_lock);
	kmem_cache_s* cp = _kmalloc_cache(size);
	if (cp == NULL) {
		kmem_lock_release(&list_lock);
		return NULL;
	}
	kmem_lock_acquire(&cp->lock);
	kmem_lock_release(&list_lock);

	void* ret = _kmem_cache_alloc(cp);
	if (ret == NULL)
		printf("Cache error: Can't allocate another slab for kmalloc!\n");
	kmem_lock_release(&cp->lock);
	return ret;
}

//find the buffer cache for a size, creating it on first use; list lock must be held
kmem_cache_t* _kmalloc_cache(size_t size) {

	//max value is 2^17
	if (size > ((size_t)1 << SIZE_CACHE_MAX)) {
		printf("Cache error: No buffer is that big!\n");
		return NULL;
	}

	int power = _ceil_log2(size);
	//min value is 2^5
	if (power < SIZE_CACHE_MIN)
		power = SIZE_CACHE_MIN;

	int idx = power - SIZE_CACHE_MIN;
	if (size_caches[idx] == NULL) {
		kmem_cache_s* c = _kmem_cache_create(size_cache_names[idx], (size_t)1 << power, NULL, NULL);
		if (c == NULL) {
			printf("Cache error: Could not create buffer cache!\n");
			return NULL;
		}
		c->is_size_cache = 1;
		size_caches[idx] = c;
	}
	return size_caches[idx];
}

void kfree(const void* objp){
	slab_s* sl = slab_of(objp);
	if (sl == NULL || !sl->cache->is_size_cache) {
		printf("Cache error: no such object allocated in buffers!\n");
		return;
	}
	//size caches are never destroyed, so the cache pointer stays valid
	kmem_cache_s* cp = sl->cache;
	kmem_lock_acquire(&cp->lock);
	_kmem_cache_free(cp, objp);
	kmem_lock_release(&cp->lock);
}

void kmem_cache_destroy(kmem_cache_t* cachep){
	kmem_lock_acquire(&list_lock);
	if (!is_cache_valid(cachep)) {
		kmem_lock_release(&list_lock);
		printf("Cache error: No such cache exists to be destroyed\n");
		return;
	}
	//update the cache list
	if (cachep == cache_list_head)
		cache_list_head = cachep->next;
	else {
		kmem_cache_s* current = cache_list_head;
		while (current->next != cachep)
			current = current->next;
		current->next = cachep->next;
	}
	kmem_lock_release(&list_lock);

	kmem_lock_acquire(&cachep->lock);
	_kmem_cache_destroy(cachep);
	kmem_lock_release(&cachep->lock);
	kmem_lock_destroy(&cachep->lock);

	//deallocate the cache itself
	buddy_deallocate(cachep, 1);
}

//release every slab; the cache must already be unlinked from the list
void _kmem_cache_destroy(kmem_cache_t* cachep) {
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
}

void kmem_cache_info(kmem_cache_t* cachep){
	if (lock_cache(cachep) == NULL)
		return;
	print_cache(cachep);
	kmem_lock_release(&cachep->lock);
}

void print_cache(kmem_cache_s* c) {
	printf("Cache name: %s\n", c->name);
	printf("Object size: %d\n", c->obj_size);
	printf("Blocks occcupied (including slabs): %d\n", c->num_of_blocks);
	printf("This cache has %d slabs right now\n", c->count_empty + c->count_full + c->count_partial);
	printf("Num of objects per slab: %d\n", c->objs_per_slab);
	printf("Num of objects allocated: %d\n", c->num_of_allocated_objs);
	int max_objs = (c->count_empty + c->count_partial + c->count_full) * c->objs_per_slab;
	if (max_objs != 0)
		printf("Occupancy: %d%%\n", 100 * c->num_of_allocated_objs / max_objs);
	printf("\n");
}


int kmem_cache_error(kmem_cache_t* cachep){
	if (lock_cache(cachep) == NULL)
		return -1;
	printf("Cache %s: %s\n", cachep->name, error_messages[cachep->last_error]);
	int ret = cachep->last_error;
	kmem_lock_release(&cachep->lock);
	return ret;
}

//helper function to see if the cachep is valid; list lock must be held
int is_cache_valid(void* cachep){
	kmem_cache_s* tmp = cache_list_head;
	while (tmp != NULL) {
		if (tmp == cachep)
			return 1;
		tmp = tmp->next;
	}
	return 0;
}
