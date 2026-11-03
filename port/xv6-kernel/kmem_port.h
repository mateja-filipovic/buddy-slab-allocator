#ifndef _KMEM_PORT_H_
#define _KMEM_PORT_H_

#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned long size_t;

//the kernel prints with printk
#define printf printk
//the kernel already has a page-level kfree, so the buffer free gets its own symbol
#define kfree kmem_kfree

typedef struct spinlock kmem_lock_t;

static inline void kmem_lock_init(kmem_lock_t* l) { initlock(l, "kmem"); }
static inline void kmem_lock_destroy(kmem_lock_t* l) { (void)l; }
static inline void kmem_lock_acquire(kmem_lock_t* l) { acquire(l); }
static inline void kmem_lock_release(kmem_lock_t* l) { release(l); }

#endif
