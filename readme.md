# Buddy-Slab memory allocator

## Project summary
An implementation of a kernel memory allocator. FrontEnd consists of a Slab Allocator, while the backend implements a Buddy Allocator.

## Some important notes
* the project is written in C
* the project builds with CMake on Windows, macOS and Linux
* everything is thread safe, sync is done with a Windows semaphore or a POSIX mutex depending on the platform
* for the project to work, the allocator must first be initialized using the `kmem_init(void*, int)` function
* `kmem_cache_shrink` only frees empty slabs if the cache has not grown since the previous shrink call
* the `tests` directory contains the automated tests, `src/main.c` is a small demo

## Usage
- clone the project
    ``` bash
    git clone https://github.com/mateja-filipovic/Buddy-Slab-Allocator.git
    ```
- configure and build
    ``` bash
    cmake -S . -B build
    cmake --build build
    ```
- run the tests
    ``` bash
    ctest --test-dir build --output-on-failure
    ```
- run the demo in `build/demo`, which creates a cache, allocates and frees objects, shrinks the cache and checks for errors
