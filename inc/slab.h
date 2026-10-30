#ifndef _slab_h_
#define _slab_h_
// File: slab.h
#include <stdlib.h>

#include "params.h"

typedef struct kmem_cache_s kmem_cache_t;

void kmem_init(void *space, int block_num);
kmem_cache_t * kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void (*dtor)(void*));// Allocate cache
int kmem_cache_shrink(kmem_cache_t *cachep); // Shrink cache
void *kmem_cache_alloc(kmem_cache_t *cachep); // Allocate one object from cache
void kmem_cache_free(kmem_cache_t *cachep, void *objp); // Deallocate one object from cache
void *kmalloc(size_t size); // Alloacate one small memory buffer
void kfree(const void *objp); // Deallocate one small memory buffer
void kmem_cache_destroy(kmem_cache_t *cachep); // Deallocate cache
void kmem_cache_info(kmem_cache_t *cachep); // Print cache info
int kmem_cache_error(kmem_cache_t *cachep); // Print error mes

//helper functions
int is_cache_valid(void* cachep);
void print_cache(struct kmem_cache_s* c);

//wrappers call these functions
int _kmem_cache_free(kmem_cache_t* cachep, const void* objp);
kmem_cache_t* _kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void (*dtor)(void*));
int _kmem_cache_shrink(kmem_cache_t* cachep);
void* _kmem_cache_alloc(kmem_cache_t* cachep);
void* _kmalloc(size_t size);
void _kfree(const void* objp);
void _kmem_cache_destroy(kmem_cache_t* cachep);
void _kmem_cache_info(kmem_cache_t* cachep);

//sync functions, used in wrappers
void mutex_wait();
void mutex_signal();

#endif
