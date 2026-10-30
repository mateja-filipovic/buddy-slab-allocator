#ifndef _CACHE_H_
#define _CACHE_H_

#include "buddy.h"

typedef struct slab_s {
	void* base_addr; //address of the object block
	void* starting_addr; //address of the first object in the object block
	void* ending_addr;

	int num_of_blocks; //how many blocks does object block take up
	int capacity; //max num of objects

	int state; // 0 - empty, 1 - partial, 2 - full
	int num_of_allocs; //objects allocated so far

	int obj_size;

	unsigned char* bit_vector; //used for managing free slots

	void (*ctor)(void*); //object constructor

	struct slab_s* next; //used by the cache manager
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

	//cache name
	char name[30];

	void (*ctor)(void*);
	void (*dtor)(void*);

	//used for making a linked list of caches
	struct kmem_cache_s* next;
}kmem_cache_s;

slab_s* allocate_a_slab(int obj_size, int block_num, void(*ctor)(void*), int offs); //allocate a new slab
void deallocate_a_slab(slab_s* sl, void(*dtor)(void*)); //destruct every object and free a slab

void* allocate_obj_from_slab(slab_s* sl); //allocate an object and initialize it
int dealloc_obj_from_slab(slab_s* sl, void* obj, void(*dtor)(void*)); //return 0 if dealloc is successful

//helper functions
int get_obj_index(slab_s* sl, void* obj); //check if the pointer to dealloc is valid
void* get_obj_address(slab_s* sl, int index);

//for bit vector manipulation
void set_bit(slab_s* sl, int index, int bit);
int get_bit(slab_s* sl, int index);

#endif