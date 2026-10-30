#ifndef _BUDDY_H_
#define _BUDDY_H_

#include "params.h"

#define POOL_MAX_SIZE 32

typedef struct buddy_s {
	void* base_addr; //first address in the space given
	void* starting_addr; //the address of the first free block (buddy metadata takes up 1 block)
	void* ending_addr; //used for error checking

	unsigned long blocks_total;
	unsigned int max_order;

	void* pool[32];
}buddy_s;

extern buddy_s* BUDDY;

void buddy_init(void* space, int numOfBlocks); //initalize
void* buddy_allocate(int sizeInBlocks); //allocate space given block size needed
void buddy_deallocate(void* blkAddr, int sizeInBlocks); //deallocate space given startign address adn block size needed

//helper functions
int _get_blk_size(int i);
int _ceil_log2(unsigned long n); //smallest order whose block count covers n
void* _get_buddy_of(void* b, int i);

#endif
