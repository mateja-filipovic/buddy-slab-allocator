#ifndef _CACHE_H_
#define _CACHE_H_

#include "buddy.h"
#include "slab.h"

struct kmem_cache_s;

typedef struct slab_s {
	void* starting_addr; //address of the first object in the object block
	void* ending_addr;

	int num_of_blocks; //how many blocks does object block take up
	int capacity; //max num of objects

	int state; // 0 - empty, 1 - partial, 2 - full
	int num_of_allocs; //objects allocated so far

	int obj_size;

	unsigned char* bit_vector; //used for managing free slots

	struct kmem_cache_s* cache;

	//used by the cache manager
	struct slab_s* prev;
	struct slab_s* next;
}slab_s;

typedef struct kmem_cache_s{
	//slab list heads
	slab_s* empty_slabs;
	slab_s* partial_slabs;
	slab_s* full_slabs;

	int count_empty;
	int count_partial;
	int count_full;

	int obj_size;
	int num_of_allocated_objs;
	int objs_per_slab;
	int blocks_per_slab;
	int num_of_blocks;

	//how many errors has this cache managed
	int num_of_errors;

	//used for L1 cache alignment
	int wasted;
	int offset;

	int growing; //a slab was added since the last shrink
	int is_size_cache; //serves kmalloc

	//cache name
	char name[30];

	void (*ctor)(void*);
	void (*dtor)(void*);

	//used for making a linked list of caches
	struct kmem_cache_s* next;
}kmem_cache_s;

//block to slab lookup table
int slab_table_init(void);
slab_s* slab_of(const void* addr);

void slab_layout(int obj_size, int block_num, int* obj_offset, int* capacity); //where objects start and how many fit

slab_s* allocate_a_slab(kmem_cache_s* cache, int offs); //allocate a new slab
void deallocate_a_slab(slab_s* sl); //destruct every object and free a slab

void* allocate_obj_from_slab(slab_s* sl); //allocate an object and initialize it
int dealloc_obj_from_slab(slab_s* sl, const void* obj); //return 0 if dealloc is successful

void slab_list_push(slab_s** head, slab_s* sl);
void slab_list_remove(slab_s** head, slab_s* sl);

//helper functions
int get_obj_index(slab_s* sl, const void* obj); //check if the pointer to dealloc is valid
void* get_obj_address(slab_s* sl, int index);

//for bit vector manipulation
void set_bit(slab_s* sl, int index, int bit);
int get_bit(slab_s* sl, int index);

#endif
