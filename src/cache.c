#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../inc/cache.h"

static slab_s** block_owner;

static unsigned long block_index(const void* addr) {
	return ((const char*)addr - (const char*)BUDDY->starting_addr) / BLOCK_SIZE;
}

int slab_table_init(void) {
	unsigned long bytes = BUDDY->blocks_total * sizeof(slab_s*);
	int blocks = (int)((bytes + BLOCK_SIZE - 1) / BLOCK_SIZE);
	block_owner = buddy_allocate(blocks);
	if (block_owner == NULL)
		return -1;
	memset(block_owner, 0, bytes);
	return 0;
}

slab_s* slab_of(const void* addr) {
	if (addr < BUDDY->starting_addr || addr >= BUDDY->ending_addr)
		return NULL;
	return block_owner[block_index(addr)];
}

void slab_layout(int obj_size, int block_num, int* obj_offset, int* capacity) {
	int total = block_num * BLOCK_SIZE;
	int bitvec_bytes = (total / obj_size + 7) / 8;
	int header = (int)sizeof(slab_s) + bitvec_bytes;
	header = (header + CACHE_L1_LINE_SIZE - 1) / CACHE_L1_LINE_SIZE * CACHE_L1_LINE_SIZE;
	*obj_offset = header;
	*capacity = (total - header) / obj_size;
}

slab_s* allocate_a_slab(kmem_cache_s* cache, int offs) {

	slab_s* sl = buddy_allocate(cache->blocks_per_slab);
	//error checking
	if (sl == NULL) {
		printf("Slab error: Can't allocate another slab!\n");
		return NULL;
	}

	int obj_offset, capacity;
	slab_layout(cache->obj_size, cache->blocks_per_slab, &obj_offset, &capacity);

	sl->starting_addr = (char*)sl + obj_offset + offs;      //always cast to char* cos of pointer arithmetic!!
	sl->ending_addr = (char*)sl + BLOCK_SIZE * cache->blocks_per_slab;

	sl->num_of_blocks = cache->blocks_per_slab;
	sl->capacity = capacity;
	sl->obj_size = cache->obj_size;

	sl->num_of_allocs = 0;
	sl->state = 0;

	sl->cache = cache;
	sl->prev = NULL;
	sl->next = NULL;
	sl->bit_vector = (unsigned char*)sl + sizeof(slab_s);
	
	memset(sl->bit_vector, 0, (capacity + 7) / 8); //svi su slobodni

	unsigned long first = block_index(sl);
	int i;
	for (i = 0; i < sl->num_of_blocks; i++)
		block_owner[first + i] = sl;

	//initialize the objects using the ctor provided!
	if (cache->ctor != NULL) {
		for (i = 0; i < sl->capacity; i++)
			(*cache->ctor)(get_obj_address(sl, i));
	}

	return sl;
}

void* allocate_obj_from_slab(slab_s* sl) {
	if (sl->capacity == sl->num_of_allocs)
		return NULL;

	int i = 0;
	while (get_bit(sl, i) != 0)
		i++;

	void* ret = get_obj_address(sl, i);
	set_bit(sl, i, 1);

	sl->num_of_allocs++;
	sl->state = (sl->num_of_allocs == sl->capacity) ? 2 : 1;

	return ret;
}

void deallocate_a_slab(slab_s* sl){
	void (*dtor)(void*) = sl->cache->dtor;
	int i;
	if (dtor != NULL) {
		for (i = 0; i < sl->capacity; i++)
			(*dtor)(get_obj_address(sl, i));
	}

	unsigned long first = block_index(sl);
	for (i = 0; i < sl->num_of_blocks; i++)
		block_owner[first + i] = NULL;

	buddy_deallocate((void*)sl, sl->num_of_blocks);
}

int dealloc_obj_from_slab(slab_s* sl, const void* obj){
	
	if (sl->num_of_allocs == 0)
		return -1; //no objs allocated
	int obj_index = get_obj_index(sl, obj);
	if (obj_index == -1)
		return -2; //the obj is not from this slab
	if (get_bit(sl, obj_index) == 0)
		return -3; //the slot is already free

	//update the free slots list
	set_bit(sl, obj_index, 0);

	sl->num_of_allocs--;
	sl->state = (sl->num_of_allocs == 0) ? 0 : 1;

	/*
	* NOTE: PROJECT REQS UNCLEAR, NOT SURE IF AN OBJECT SHOULD BE INITIALIZED
	* AFTER BEING DEALLOCATED, OR JUST DESTRUCTED
	* UNCOMMENT IF NECESSARY
	* if (sl->cache->ctor != NULL)
		* (*sl->cache->ctor)(obj);
	*/

	return 0;
}

void slab_list_push(slab_s** head, slab_s* sl) {
	sl->prev = NULL;
	sl->next = *head;
	if (*head != NULL)
		(*head)->prev = sl;
	*head = sl;
}

void slab_list_remove(slab_s** head, slab_s* sl) {
	if (sl->prev != NULL)
		sl->prev->next = sl->next;
	else
		*head = sl->next;
	if (sl->next != NULL)
		sl->next->prev = sl->prev;
	sl->prev = NULL;
	sl->next = NULL;
}

//get the bit vector index of an object slot
int get_obj_index(slab_s* sl, const void* obj) {
	int index = -1;
	if (obj >= sl->ending_addr || obj < sl->starting_addr)
		return index;
	size_t calc = (const char*)obj - (const char*)sl->starting_addr;
	if (calc % sl->obj_size != 0)
		return index;
	if (calc / sl->obj_size >= (size_t)sl->capacity)
		return index;
	return (int)(calc / sl->obj_size);
}

//get object address using a bit vector index
void* get_obj_address(slab_s* sl, int index){
	void* ret = (char*)sl->starting_addr + index * sl->obj_size;
	return ret;
}

void set_bit(slab_s* sl, int index, int bit) {
	//find the word
	int word = index >> 3;
	int n = sizeof(index) * 8 - 3;
	int offset = ((unsigned)index << n) >> n;
	if (bit) {
		sl->bit_vector[word] |= 1 << (7 - offset);
	}
	else {
		sl->bit_vector[word] &= ~(1 << (7 - offset));
	}
}

int get_bit(slab_s* sl, int index) {
	//find the word
	int word = index >> 3;
	int n = sizeof(index) * 8 - 3;
	int offset = ((unsigned)index << n) >> n;
	//check what value the bit has
	if (sl->bit_vector[word] & (1 << (7 - offset))) {
		return 1;
	}
	else {
		return 0;
	}
}
