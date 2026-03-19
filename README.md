# pthread Schedule Module Documentation

This project provides a lightweight scheduler abstraction for splitting iteration work across POSIX threads.

## Files

- `include/pthread_schedule.h`: scheduler API, policy enum, and data structures
- `src/pthread_schedule.c`: scheduler implementations (`static`, `dynamic`, `guided`) and policy dispatcher
- `examples/count_even_example.c`: Complete example using the library

## Building and Running

This project provides both a `Makefile` and `CMakeLists.txt` for easy builds.

### Using Make
```bash
make all        # Builds the static library and examples
./bin/count_even_example
```

### Using CMake
```bash
mkdir -p build && cd build
cmake ..
make
./count_even_example
```

## Scheduling Policies

### `SCHEDULE_STATIC`

- Computes each thread range once, based on thread index.
- Uses balanced partitioning:
  - Every thread gets `total_iterations / num_threads`
  - First `total_iterations % num_threads` threads get one extra iteration
- No mutex required.
- Best when per-iteration work cost is uniform.

### `SCHEDULE_DYNAMIC`

- Threads fetch fixed-size chunks from a shared counter.
- Requires `shared_counter`, `lock`, and positive `chunk_size`.
- Better load balance when work is irregular.
- More synchronization overhead due to frequent locking.

### `SCHEDULE_GUIDED`

- Threads fetch decreasing chunk sizes as work runs out.
- Chunk is computed as `ceil(remaining / num_threads)`, clamped to a minimum `chunk_size`.
- Requires `shared_counter`, `lock`, and positive `chunk_size`.
- Often a good compromise between dynamic load balance and lock overhead.

## API Summary

```c
chunk_t schedule_static(schedule_context_t *context);
chunk_t schedule_dynamic(schedule_context_t *context);
chunk_t schedule_guided(schedule_context_t *context);
chunk_t schedule_next_chunk(schedule_context_t *context, schedule_policy_t policy);
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
```

`chunk_t`:

- `start_index`: inclusive start
- `end_index`: exclusive end
- `done`: non-zero means no more work

`schedule_execute(...)` is a convenience API that repeatedly fetches chunks for the selected policy and invokes a callback for each chunk. This avoids writing `while` loops manually in every worker.

`schedule_parallel_for(...)` is a higher-level API that also manages thread creation/join and shared scheduling state. This avoids writing custom boilerplate.

## Build Check (Scheduler Unit)

The scheduler implementation compiles with strict warning flags:

```bash
gcc -Wall -Wextra -Wpedantic -std=c11 -Iinclude src/pthread_schedule.c -c -o /tmp/pthread_schedule.o
```
