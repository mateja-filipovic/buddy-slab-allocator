#include <string.h>
#include <stdio.h>
#include <math.h>
#include <windows.h>

#include "../inc/slab.h"
#include "../inc/buddy.h"
#include "../inc/cache.h"

//pointer to the first cache
kmem_cache_s* cache_list_head = NULL;
//mutex used for thread safety
HANDLE mtx;


void kmem_init(void* space, int block_num){

	//initializing the mutex
	mtx = CreateSemaphore(NULL, 1, 1, NULL);
	if (mtx == NULL) {
		printf("Error: Cannot alocate a windows.h semaphore");
		return;
	}

	//initalize the buddy allocator
	buddy_init(space, block_num);

	//initialize small memory buffers
	int i;
	for (i = 0; i < 13; i++) { //sizes from 2^5 to 2^17
		char buffer_name[30];
		sprintf_s(buffer_name, 30, "size-2^%d", i+5);
		int pw = i + 5;
		size_t size = (size_t)pow(2, pw);
		kmem_cache_create(buffer_name, size, NULL, NULL);
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
	
	//initializing the members
	cache->empty_slabs = NULL;
	cache->partial_slabs = NULL;
	cache->full_slabs = NULL;

	cache->count_empty = 0;
	cache->count_partial = 0;
	cache->count_full = 0;

	cache->obj_size = size;

	cache->num_of_allocated_objs = 0;
	cache->num_of_blocks = 1;
	cache->objs_per_slab = (block_num * BLOCK_SIZE) / size;
	cache->blocks_per_slab = block_num;

	cache->num_of_errors = 0;

	cache->wasted = (block_num * BLOCK_SIZE) % size;
	cache->offset = 0;

	strcpy_s(cache->name, 30, name);

	cache->ctor = ctor;
	cache->dtor = dtor;
	cache->next = cache_list_head;

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

	//find and deallocate all empty slabs
	slab_s* tmp = cachep->empty_slabs, * helper = NULL;
	while (tmp != NULL) {
		//num_freed++;
		helper = tmp->next;
		cachep->num_of_blocks -= (cachep->blocks_per_slab + 1);
		num_freed += cachep->blocks_per_slab + 1;
		deallocate_a_slab(tmp, cachep->dtor);
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

	void* ret;
	//try to allocate an object in partialy filled slabs
	if (cachep->partial_slabs != NULL) {
		//print_slab(cachep->partial_slabs);
		ret = allocate_obj_from_slab(cachep->partial_slabs);
		//move the slab to full slab list if needed
		if (cachep->partial_slabs->state == 2) {
			slab_s* tmp = cachep->partial_slabs;
			cachep->partial_slabs = cachep->partial_slabs->next;
			tmp->next = cachep->full_slabs;
			cachep->full_slabs = tmp;
			cachep->count_partial--;
			cachep->count_full++;
		}
	}
	//try to allocate an object in empty slabs
	else if (cachep->empty_slabs != NULL) {
		ret = allocate_obj_from_slab(cachep->empty_slabs);
		//move the slab to partials and update empty list
		slab_s* tmp = cachep->empty_slabs;
		cachep->empty_slabs = cachep->empty_slabs->next;
		tmp->next = cachep->partial_slabs;
		cachep->partial_slabs = tmp;
		cachep->count_empty--;
		cachep->count_partial++;
	}

	else { //must allocate a new slab
		slab_s* tmp = allocate_a_slab(cachep->obj_size, cachep->blocks_per_slab, cachep->ctor, cachep->offset);
		//error checking!
		if (tmp == NULL) {
			printf("Cache rror: Can't allocate a slab\n");
			cachep->num_of_errors++;
			return NULL;
		}
		cachep->offset += CACHE_L1_LINE_SIZE;
		if (cachep->offset > cachep->wasted)
			cachep->offset = 0;
		cachep->num_of_blocks += cachep->blocks_per_slab + 1;
		
		ret = allocate_obj_from_slab(tmp);
		
		//update partials list
		tmp->next = cachep->partial_slabs;
		cachep->partial_slabs = tmp;
		cachep->count_partial++;
	}

	cachep->num_of_allocated_objs++;
	return ret;
}

void kmem_cache_free(kmem_cache_t* cachep, void* objp) {
	mutex_wait();
	_kmem_cache_free(cachep, objp, 0);
	mutex_signal();
}

int _kmem_cache_free(kmem_cache_t* cachep, void* objp, int is_internal) {
	//is it a valid cache pointer
	if (!is_cache_valid(cachep)) {
		printf("Cache error: Unable to dealloc object, no such cache exists\n");
		return -1;
	}

	//is the object in the partials
	slab_s* tmp = cachep->partial_slabs, * prev = NULL;
	while (tmp != NULL) {
		//try to dealloc from a slab
		//if the obj is deallocated the function returns 0
		if (dealloc_obj_from_slab(tmp, objp, cachep->dtor) == 0) {
			cachep->num_of_allocated_objs--;
			//move to empty list if needed
			if (tmp->state == 0) {
				//if this is the first partial - meaning the only partial, reset the partial pointer
				if (tmp == cachep->partial_slabs)
					cachep->partial_slabs = cachep->partial_slabs->next;
				//if not just move the pointer
				else
					prev->next = tmp->next;
				tmp->next = cachep->empty_slabs;
				cachep->empty_slabs = tmp;
				cachep->count_partial--;
				cachep->count_empty++;
			}
			return 0;
		}
		prev = tmp;
		tmp = tmp->next;
	}

	//try to dealloc from full slabs
	tmp = cachep->full_slabs;
	prev = NULL;
	while (tmp != NULL) {
		if (dealloc_obj_from_slab(tmp, objp, cachep->dtor) == 0) {
			cachep->num_of_allocated_objs--;
			//move the slab to partials

			//check if this is the list head
			if (tmp == cachep->full_slabs)
				cachep->full_slabs = cachep->full_slabs->next;
			//if it's not just move the pointer
			else
				prev->next = tmp->next;

			tmp->next = cachep->partial_slabs;
			cachep->partial_slabs = tmp;
			cachep->count_partial++;
			cachep->count_full--;

			return 0;
		}
		prev = tmp;
		tmp = tmp->next;
	}

	//if the user called this function, print an error
	if (is_internal == 0) {
		printf("Cache error: No such object exists!\n");
		cachep->num_of_errors++;
	}
	return -1;
}

void* kmalloc(size_t size){
	mutex_wait();
	void* ret = _kmalloc(size);
	mutex_signal();
	return ret;
}

void* _kmalloc(size_t size) {

	kmem_cache_s* cp;

	//max value is 2^17
	if (size > (size_t)pow(2, 17)) {
		printf("Cache error: No buffer is that big!\n");
		return NULL;
	}

	int power = (int)ceil(log((double)size) / log(2.0));
	//min value is 2^5
	if (power < 5)
		power = 5;

	//find the buffer
	cp = cache_list_head;
	char buffer_name[30];
	sprintf_s(buffer_name, 30, "size-2^%d", power);
	while (cp != NULL) {
		if (strcmp(buffer_name, cp->name) == 0)
			break;
		cp = cp->next;
	}

	//more error checking, might be redundant!
	if (cp == NULL) {
		printf("Cache error: Could not find buffer!\n");
		return NULL;
	}

	//no space to allocate another 2^5 slab!
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

	kmem_cache_s* cp = cache_list_head;
	char firstBuffer[30];
	sprintf_s(firstBuffer, 30, "size-2^%d", 17); //2^17 is the first buffer in the cache list
	while (cp != NULL) {
		if (strcmp(cp->name, firstBuffer) == 0)
			break;
		cp = cp->next;
	}
	if (cp == NULL) {
		printf("Cache error: unable to find a buffer to free from!\n");
		return;
	}
	//try to dealloc from every buffer
	while (cp != NULL) {
		if (_kmem_cache_free(cp, objp, 1) == 0)
			return;
		cp = cp->next;
	}
	printf("Cache error: no such object allocated in buffers!\n");
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
		deallocate_a_slab(tmp, cachep->dtor);
		tmp = helper;
	}
	//deallocate partial slabs
	tmp = cachep->partial_slabs, helper = NULL;
	while (tmp != NULL) {
		helper = tmp->next;
		deallocate_a_slab(tmp, cachep->dtor);
		tmp = helper;
	}

	//deallocate full slabs
	tmp = cachep->full_slabs, helper = NULL;
	while (tmp != NULL) {
		helper = tmp->next;
		deallocate_a_slab(tmp, cachep->dtor);
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

//mutex sync functions
void mutex_wait() { WaitForSingleObject(mtx, INFINITE);}
void mutex_signal() { ReleaseSemaphore(mtx, 1, NULL); }

