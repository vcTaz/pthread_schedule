#ifndef PTHREAD_SCHEDULE_H
#define PTHREAD_SCHEDULE_H

#include <stdatomic.h>

typedef enum schedule_policy {
  SCHEDULE_STATIC = 0,
  SCHEDULE_DYNAMIC = 1,
  SCHEDULE_GUIDED = 2,
  SCHEDULE_MAX
} schedule_policy_t;

typedef struct {
  int thread_id;
  int num_threads;
  long total_iterations;
  long chunk_size;
  atomic_long *shared_counter;
  long local_state;
  atomic_int *cancel;
} schedule_context_t;

typedef struct {
  long start_index;
  long end_index;
  int done;
} chunk_t;

typedef void (*schedule_chunk_callback_t)(chunk_t chunk, void *user_data);
typedef int (*schedule_parallel_chunk_callback_t)(chunk_t chunk, int thread_id,
                                                  void *user_data);
typedef long (*schedule_parallel_chunk_reduce_long_t)(chunk_t chunk,
                                                      int thread_id,
                                                      void *user_data,
                                                      int *error);

/* Opaque thread pool handle */
typedef struct schedule_thread_pool schedule_thread_pool_t;

#ifdef __cplusplus
extern "C" {
#endif

chunk_t schedule_static(schedule_context_t *context);
chunk_t schedule_dynamic(schedule_context_t *context);
chunk_t schedule_guided(schedule_context_t *context);
chunk_t schedule_next_chunk(schedule_context_t *context,
                            schedule_policy_t policy);

int schedule_execute(schedule_context_t *context, schedule_policy_t policy,
                     schedule_chunk_callback_t callback, void *user_data);

int schedule_parallel_for(long total_iterations, int num_threads,
                          long chunk_size, schedule_policy_t policy,
                          schedule_parallel_chunk_callback_t callback,
                          void *user_data);

int schedule_parallel_reduce_long(
    long total_iterations, int num_threads, long chunk_size,
    schedule_policy_t policy, schedule_parallel_chunk_reduce_long_t callback,
    void *user_data, long *out_result);

/* --- Thread Pool API --- */
schedule_thread_pool_t *schedule_thread_pool_create(int num_threads);
void schedule_thread_pool_destroy(schedule_thread_pool_t *pool);

int schedule_parallel_for_pool(schedule_thread_pool_t *pool,
                               long total_iterations, long chunk_size,
                               schedule_policy_t policy,
                               schedule_parallel_chunk_callback_t callback,
                               void *user_data);

int schedule_parallel_reduce_long_pool(
    schedule_thread_pool_t *pool, long total_iterations, long chunk_size,
    schedule_policy_t policy, schedule_parallel_chunk_reduce_long_t callback,
    void *user_data, long *out_result);

#ifdef __cplusplus
}
#endif

#endif /* PTHREAD_SCHEDULE_H */
