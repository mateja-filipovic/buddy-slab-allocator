<h1 align="center">Buddy-Slab Allocator</h1>

<p align="center">
  A kernel-style memory allocator in C: slab caches on top of a buddy system.
</p>

<p align="center">
  <b>English</b> | <a href="README.sr.md">Srpski</a>
</p>

<p align="center">
  <a href="#build">Build</a> ·
  <a href="#run">Run</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#design">Design</a> ·
  <a href="#tests">Tests</a> ·
  <a href="#project-layout">Project layout</a>
</p>

---

This is a project for the Operating Systems 2 course at the School of Electrical Engineering, University of Belgrade. The allocator manages a contiguous region of memory handed to it at startup, in 4 KB blocks. A buddy system keeps track of free blocks, and a slab layer on top of it serves fixed-size objects from caches, with optional constructors and destructors, plus power-of-two small buffers through `kmalloc` and `kfree`. The public interface in `inc/slab.h` follows the header handed out with the assignment.

It builds and runs on macOS, Linux and Windows and ships with a demo and an automated test suite.

## Build

Needs a C compiler and CMake 3.16 or newer.

| Platform | Compiler | Install CMake |
|---|---|---|
| macOS | Xcode command line tools (`xcode-select --install`) | `brew install cmake` |
| Linux | gcc or clang | `sudo apt install cmake` or your distribution's equivalent |
| Windows | Visual Studio with the Desktop development with C++ workload | `winget install Kitware.CMake` or the Visual Studio installer |

```sh
git clone https://github.com/mateja-filipovic/Buddy-Slab-Allocator.git
cd Buddy-Slab-Allocator
cmake -S . -B build
cmake --build build
```

On Windows, CMake picks the Visual Studio generator and the binaries land in `build/Debug/`. You can also open the folder directly in Visual Studio or CLion, both of which understand `CMakeLists.txt` without any further setup.

## Run

**Demo.** Creates a cache, allocates and frees objects, shrinks the cache and prints its state along the way.

```sh
./build/demo            # macOS and Linux
build\Debug\demo.exe    # Windows
```

**Tests.**

```sh
ctest --test-dir build --output-on-failure            # macOS and Linux
ctest --test-dir build --output-on-failure -C Debug   # Windows
```

## Usage

Hand the allocator a region of memory once, then create caches for the object types you need.

```c
#include "slab.h"

void *space = malloc(1000 * BLOCK_SIZE);
kmem_init(space, 1000);

kmem_cache_t *pcb_cache = kmem_cache_create("PCB", sizeof(struct pcb), pcb_ctor, pcb_dtor);

struct pcb *p = kmem_cache_alloc(pcb_cache);
kmem_cache_free(pcb_cache, p);

char *buf = kmalloc(300);   /* served from the size-512 cache */
kfree(buf);

kmem_cache_shrink(pcb_cache);
kmem_cache_destroy(pcb_cache);
```

| Function | Purpose |
|---|---|
| `kmem_init(space, blocks)` | Initialise the allocator over `blocks` blocks of `BLOCK_SIZE` bytes starting at `space` |
| `kmem_cache_create(name, size, ctor, dtor)` | Create a cache for objects of `size` bytes; `ctor` and `dtor` may be `NULL` |
| `kmem_cache_alloc(cache)` | Allocate one object |
| `kmem_cache_free(cache, obj)` | Return one object |
| `kmem_cache_shrink(cache)` | Release empty slabs, returns the number of blocks freed |
| `kmem_cache_destroy(cache)` | Release every slab and the cache itself |
| `kmalloc(size)` | Allocate a small buffer, rounded up to a power of two between 32 bytes and 128 KB |
| `kfree(ptr)` | Return a small buffer |
| `kmem_cache_info(cache)` | Print the cache's name, object size, block count, slab count, objects per slab and occupancy |
| `kmem_cache_error(cache)` | Print and return the number of errors recorded for the cache |

Every call is thread safe. Invalid frees, double frees and allocations that cannot be satisfied are reported through `kmem_cache_error` rather than crashing.

## Design

**Buddy system.** Free blocks are kept in per-order lists of power-of-two sizes. An allocation takes the smallest free block that fits and splits it down; a free merges the block with its buddy as long as the buddy is also free. The first block of the region holds the buddy's own bookkeeping.

**Slab caches.** Each cache keeps three lists of slabs: empty, partial and full. A slab is a power-of-two run of blocks whose first bytes hold the slab header and a bitmap of free slots, followed by the objects. Allocation prefers partial slabs, then empty ones, and grows the cache with a new slab only when neither exists. Successive slabs start their objects at different cache-line offsets so that objects from different slabs do not compete for the same L1 lines.

**Constructors and destructors.** The constructor runs once per slot when a slab is built. A freed object is left as it is, so the caller is expected to return it in a constructed state. The destructor runs on every slot when a slab is released, either by shrinking or by destroying the cache.

**Shrinking.** `kmem_cache_shrink` releases empty slabs only if the cache has not grown since the previous shrink. The first call after growth resets that flag and frees nothing, which stops a cache from thrashing between growing and shrinking.

**Small buffers.** `kmalloc` serves sizes from 32 bytes to 128 KB out of thirteen size caches, one per power of two. `kfree` finds the owning slab through a block-to-slab table that the allocator keeps inside the managed region, so no scan is needed.

**Memory.** The allocator uses only the region it was given. The buddy metadata, the block table, cache descriptors and slabs all come out of that region.

**Synchronisation.** One lock guards the whole allocator: a semaphore on Windows and a pthread mutex elsewhere. The platform-specific part is a few lines at the top of `src/slab.c`.

## Tests

The suite lives in `tests/` and runs under ctest. Each test is a standalone program that prints `PASS` or a `FAIL` line with the file and line of the failing check.

| Test | Covers |
|---|---|
| `buddy_split` | Split halves enter the free list with a valid link after earlier use |
| `size_class` | Power-of-two rounding for `kmalloc` and the buddy's maximum order |
| `double_free` | A second free of the same object is rejected and counted |
| `destructor` | Constructor and destructor each run once per slot |
| `failed_slab` | A slab that cannot be allocated leaves the heap intact |
| `shrink` | The grown flag and the block count returned by shrink |
| `cache_free` | Frees to the wrong cache, misaligned pointers, and moving slabs between lists |
| `stress` | 20 000 random allocations and frees across caches and `kmalloc`, with contents verified |

To run the suite under the sanitizers, add `-fsanitize=address,undefined` to the compiler flags, for example with `cmake -S . -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"`.

## Project layout

```
inc/params.h    block size and cache-line size
inc/buddy.h     buddy system
inc/cache.h     slab and cache structures, slab bookkeeping
inc/slab.h      public interface, as handed out with the assignment
src/buddy.c     buddy allocation, deallocation and coalescing
src/cache.c     slab layout, bitmap, block-to-slab table, slab lists
src/slab.c      the public interface, locking, size caches
src/main.c      demo
tests/          ctest suite
```

## Known limits

- A single lock serialises every call. Per-cache locking would allow more concurrency.
- `kfree` accepts only buffers that came from `kmalloc`. Objects from user caches must go back through `kmem_cache_free`.
- Objects are packed at their exact size, so sizes that are not a multiple of the platform's alignment yield unaligned objects.
- Cache names longer than 29 characters are truncated.
