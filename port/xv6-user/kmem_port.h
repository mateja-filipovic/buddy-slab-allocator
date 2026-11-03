#ifndef _KMEM_PORT_H_
#define _KMEM_PORT_H_

#include "kernel/types.h"
#include "user/user.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned long size_t;

//the public test runs as a single process, so no locking is needed
typedef struct { int unused; } kmem_lock_t;

static inline void kmem_lock_init(kmem_lock_t* l) { (void)l; }
static inline void kmem_lock_destroy(kmem_lock_t* l) { (void)l; }
static inline void kmem_lock_acquire(kmem_lock_t* l) { (void)l; }
static inline void kmem_lock_release(kmem_lock_t* l) { (void)l; }

#endif
