# pthread_schedule

`pthread_schedule` is a compact C11 library for distributing loop iterations across POSIX threads with static, dynamic, and guided scheduling policies.

It is designed to be small, reusable, and easy to integrate into larger native codebases. The repository includes:

- A public scheduling API
- One-shot parallel loop helpers
- `long` reduction helpers
- A reusable thread-pool interface
- A complete example program

## Repository Layout

- `include/pthread_schedule.h`: public API declarations
- `src/pthread_schedule.c`: scheduling implementations and thread-pool support
- `examples/count_even_example.c`: demonstration program
- `Makefile`: conventional Unix-style build
- `CMakeLists.txt`: CMake build definition

## Requirements

- C11-compatible compiler
- POSIX threads implementation
- CMake 3.10 or newer when using CMake

Note: this project targets POSIX threads. On Windows, a compatible pthreads environment is required.

## Scheduling Policies

### `SCHEDULE_STATIC`

- Deterministic work assignment by thread index
- Best when each iteration has similar cost
- Lowest coordination overhead
- Supports contiguous partitioning when `chunk_size == 0`

### `SCHEDULE_DYNAMIC`

- Fixed-size chunks fetched from a shared counter
- Better load balancing for irregular work
- Higher synchronization overhead than static scheduling
- Requires `chunk_size > 0`

### `SCHEDULE_GUIDED`

- Chunk sizes shrink as the remaining work decreases
- Useful when work is uneven but coordination cost still matters
- Requires `chunk_size > 0`

## Build

### Make

```bash
make
./bin/count_even_example
```

### CMake

```bash
cmake -S . -B build
cmake --build build
./build/count_even_example
```

## Public API Overview

Chunk selection:

```c
chunk_t schedule_static(schedule_context_t *context);
chunk_t schedule_dynamic(schedule_context_t *context);
chunk_t schedule_guided(schedule_context_t *context);
chunk_t schedule_next_chunk(schedule_context_t *context, schedule_policy_t policy);
```

Execution helpers:

```c
int schedule_execute(schedule_context_t *context,
                     schedule_policy_t policy,
                     schedule_chunk_callback_t callback,
                     void *user_data);

int schedule_parallel_for(long total_iterations,
                          int num_threads,
                          long chunk_size,
                          schedule_policy_t policy,
                          schedule_parallel_chunk_callback_t callback,
                          void *user_data);

int schedule_parallel_reduce_long(long total_iterations,
                                  int num_threads,
                                  long chunk_size,
                                  schedule_policy_t policy,
                                  schedule_parallel_chunk_reduce_long_t callback,
                                  void *user_data,
                                  long *out_result);
```

Thread-pool helpers:

```c
schedule_thread_pool_t *schedule_thread_pool_create(int num_threads);
void schedule_thread_pool_destroy(schedule_thread_pool_t *pool);

int schedule_parallel_for_pool(schedule_thread_pool_t *pool,
                               long total_iterations,
                               long chunk_size,
                               schedule_policy_t policy,
                               schedule_parallel_chunk_callback_t callback,
                               void *user_data);

int schedule_parallel_reduce_long_pool(
    schedule_thread_pool_t *pool,
    long total_iterations,
    long chunk_size,
    schedule_policy_t policy,
    schedule_parallel_chunk_reduce_long_t callback,
    void *user_data,
    long *out_result);
```

The public header contains the authoritative API comments.

## Example

[`examples/count_even_example.c`](./examples/count_even_example.c) measures the three scheduling policies while counting even values across a large integer array.

## License

This project is distributed under the terms described in [`LICENSE`](./LICENSE).
