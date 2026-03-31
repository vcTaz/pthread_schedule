#ifndef PTHREAD_SCHEDULE_H
#define PTHREAD_SCHEDULE_H

#include <stdatomic.h>

/**
 * @brief Scheduling policy used to assign loop iterations.
 */
typedef enum schedule_policy {
  SCHEDULE_STATIC = 0,
  SCHEDULE_DYNAMIC = 1,
  SCHEDULE_GUIDED = 2,
  SCHEDULE_MAX
} schedule_policy_t;

/**
 * @brief Mutable scheduling state consumed by chunk-selection functions.
 */
typedef struct {
  int thread_id;               /**< Zero-based worker identifier. */
  int num_threads;             /**< Total number of participating workers. */
  long total_iterations;       /**< Number of loop iterations to distribute. */
  long chunk_size;             /**< Policy-specific chunk size hint. */
  atomic_long *shared_counter; /**< Shared index for dynamic/guided policies. */
  long local_state;            /**< Per-worker state for static scheduling. */
  atomic_int *cancel;          /**< Optional cooperative cancellation flag. */
} schedule_context_t;

/**
 * @brief Half-open iteration range returned by the scheduler.
 */
typedef struct {
  long start_index; /**< Inclusive start index. */
  long end_index;   /**< Exclusive end index. */
  int done;         /**< Non-zero when no more work remains. */
} chunk_t;

/** @brief Callback invoked for each scheduled chunk. */
typedef void (*schedule_chunk_callback_t)(chunk_t chunk, void *user_data);

/**
 * @brief Callback used by parallel execution helpers.
 *
 * Return `0` to continue execution. Any non-zero value requests early
 * termination and causes the enclosing helper to report failure.
 */
typedef int (*schedule_parallel_chunk_callback_t)(chunk_t chunk, int thread_id,
                                                  void *user_data);

/**
 * @brief Reduction callback used by `long` reduction helpers.
 *
 * The callback returns the partial result for `chunk` and may set `*error` to
 * a non-zero value to signal failure.
 */
typedef long (*schedule_parallel_chunk_reduce_long_t)(chunk_t chunk,
                                                      int thread_id,
                                                      void *user_data,
                                                      int *error);

/** @brief Opaque reusable worker-pool handle. */
typedef struct schedule_thread_pool schedule_thread_pool_t;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Request the next chunk using static scheduling. */
chunk_t schedule_static(schedule_context_t *context);

/** @brief Request the next chunk using dynamic scheduling. */
chunk_t schedule_dynamic(schedule_context_t *context);

/** @brief Request the next chunk using guided scheduling. */
chunk_t schedule_guided(schedule_context_t *context);

/** @brief Request the next chunk using the selected scheduling policy. */
chunk_t schedule_next_chunk(schedule_context_t *context,
                            schedule_policy_t policy);

/**
 * @brief Process scheduled chunks on the current thread until completion.
 *
 * Returns `0` on success and `-1` on invalid input.
 */
int schedule_execute(schedule_context_t *context, schedule_policy_t policy,
                     schedule_chunk_callback_t callback, void *user_data);

/**
 * @brief Execute a parallel loop by creating and joining worker threads.
 *
 * Returns `0` on success and `-1` on invalid input or worker failure.
 */
int schedule_parallel_for(long total_iterations, int num_threads,
                          long chunk_size, schedule_policy_t policy,
                          schedule_parallel_chunk_callback_t callback,
                          void *user_data);

/**
 * @brief Execute a parallel reduction that accumulates `long` values.
 *
 * On success, stores the reduced value in `out_result` and returns `0`.
 */
int schedule_parallel_reduce_long(
    long total_iterations, int num_threads, long chunk_size,
    schedule_policy_t policy, schedule_parallel_chunk_reduce_long_t callback,
    void *user_data, long *out_result);

/** @brief Create a reusable thread pool. */
schedule_thread_pool_t *schedule_thread_pool_create(int num_threads);

/** @brief Destroy a thread pool and stop any in-flight work. */
void schedule_thread_pool_destroy(schedule_thread_pool_t *pool);

/** @brief Execute a parallel loop on an existing thread pool. */
int schedule_parallel_for_pool(schedule_thread_pool_t *pool,
                               long total_iterations, long chunk_size,
                               schedule_policy_t policy,
                               schedule_parallel_chunk_callback_t callback,
                               void *user_data);

/** @brief Execute a `long` reduction on an existing thread pool. */
int schedule_parallel_reduce_long_pool(
    schedule_thread_pool_t *pool, long total_iterations, long chunk_size,
    schedule_policy_t policy, schedule_parallel_chunk_reduce_long_t callback,
    void *user_data, long *out_result);

#ifdef __cplusplus
}
#endif

#endif /* PTHREAD_SCHEDULE_H */
