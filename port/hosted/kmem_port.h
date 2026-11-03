#ifndef _KMEM_PORT_H_
#define _KMEM_PORT_H_

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { HANDLE sem; } kmem_lock_t;

static inline void kmem_lock_init(kmem_lock_t* l) { l->sem = CreateSemaphore(NULL, 1, 1, NULL); }
static inline void kmem_lock_destroy(kmem_lock_t* l) { CloseHandle(l->sem); }
static inline void kmem_lock_acquire(kmem_lock_t* l) { WaitForSingleObject(l->sem, INFINITE); }
static inline void kmem_lock_release(kmem_lock_t* l) { ReleaseSemaphore(l->sem, 1, NULL); }

#else
#include <pthread.h>

typedef struct { pthread_mutex_t mtx; } kmem_lock_t;

static inline void kmem_lock_init(kmem_lock_t* l) { pthread_mutex_init(&l->mtx, NULL); }
static inline void kmem_lock_destroy(kmem_lock_t* l) { pthread_mutex_destroy(&l->mtx); }
static inline void kmem_lock_acquire(kmem_lock_t* l) { pthread_mutex_lock(&l->mtx); }
static inline void kmem_lock_release(kmem_lock_t* l) { pthread_mutex_unlock(&l->mtx); }

#endif

#endif
