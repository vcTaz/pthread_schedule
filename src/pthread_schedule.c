#include "../include/pthread_schedule.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Cache-line padding                                                  */
/* ------------------------------------------------------------------ */

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

_Static_assert(sizeof(atomic_long) < CACHE_LINE_SIZE,
               "atomic_long must be smaller than CACHE_LINE_SIZE");
_Static_assert(sizeof(long) < CACHE_LINE_SIZE,
               "long must be smaller than CACHE_LINE_SIZE");

typedef struct {
  atomic_long value;
  char padding[CACHE_LINE_SIZE - sizeof(atomic_long)];
} padded_atomic_long_t;

typedef struct {
  long value;
  char padding[CACHE_LINE_SIZE - sizeof(long)];
} padded_long_t;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static chunk_t (*const schedule_functions[])(schedule_context_t *) = {
    [SCHEDULE_STATIC] = schedule_static,
    [SCHEDULE_DYNAMIC] = schedule_dynamic,
    [SCHEDULE_GUIDED] = schedule_guided};

/* Sentinel for "no more work". Callers must check chunk.done; never rely on
 * start_index == end_index == 0 to detect completion. */
static chunk_t done_chunk(void) { return (chunk_t){0, 0, 1}; }

static int has_basic_context(const schedule_context_t *context) {
  return context != NULL && context->num_threads > 0 &&
         context->thread_id >= 0 && context->thread_id < context->num_threads &&
         context->total_iterations >= 0;
}

/* Return 1 if the cancel flag is set, 0 otherwise. */
static int is_cancelled(const schedule_context_t *context) {
  return context->cancel != NULL && atomic_load(context->cancel) != 0;
}

/* Normalize chunk_size: for dynamic/guided, default to 1 when <= 0
 * (matching OpenMP defaults). Static with 0 already means even
 * partitioning, so it is left alone. */
static long normalise_chunk_size(schedule_policy_t policy, long chunk_size) {
  if (policy == SCHEDULE_STATIC)
    return chunk_size;
  return (chunk_size <= 0) ? 1 : chunk_size;
}

/* ------------------------------------------------------------------ */
/* Scheduling algorithms                                               */
/* ------------------------------------------------------------------ */

chunk_t schedule_static(schedule_context_t *context) {
  if (!has_basic_context(context)) {
    return done_chunk();
  }

  if (context->chunk_size > 0) {
    long stride = (long)context->num_threads * context->chunk_size;
    long start_index = (long)context->thread_id * context->chunk_size +
                       context->local_state * stride;

    if (start_index >= context->total_iterations) {
      return done_chunk();
    }

    long end_index = start_index + context->chunk_size;
    if (end_index > context->total_iterations) {
      end_index = context->total_iterations;
    }

    context->local_state++;
    return (chunk_t){start_index, end_index, 0};
  }

  /* chunk_size == 0 means one contiguous block per thread. */
  if (context->local_state != 0) {
    return done_chunk();
  }
  context->local_state = 1;

  long base_chunk = context->total_iterations / context->num_threads;
  long remainder = context->total_iterations % context->num_threads;
  long start_index =
      context->thread_id * base_chunk +
      ((context->thread_id < remainder) ? context->thread_id : remainder);
  long my_size = base_chunk + ((context->thread_id < remainder) ? 1 : 0);
  long end_index = start_index + my_size;

  return (chunk_t){start_index, end_index,
                   start_index >= context->total_iterations};
}

chunk_t schedule_dynamic(schedule_context_t *context) {
  if (!has_basic_context(context) || context->shared_counter == NULL ||
      context->chunk_size <= 0) {
    return done_chunk();
  }

  long start_index =
      atomic_fetch_add(context->shared_counter, context->chunk_size);
  if (start_index >= context->total_iterations) {
    return done_chunk();
  }

  long end_index = start_index + context->chunk_size;
  if (end_index > context->total_iterations) {
    end_index = context->total_iterations;
  }

  return (chunk_t){start_index, end_index, 0};
}

chunk_t schedule_guided(schedule_context_t *context) {
  if (!has_basic_context(context) || context->shared_counter == NULL ||
      context->chunk_size <= 0) {
    return done_chunk();
  }

  long start_index = atomic_load(context->shared_counter);
  long end_index;

  do {
    long remaining_iterations = context->total_iterations - start_index;
    if (remaining_iterations <= 0) {
      return done_chunk();
    }

    long cs = (remaining_iterations + context->num_threads - 1) /
              context->num_threads;
    if (cs < context->chunk_size) {
      cs = context->chunk_size;
    }

    end_index = start_index + cs;
    if (end_index > context->total_iterations) {
      end_index = context->total_iterations;
    }
  } while (!atomic_compare_exchange_weak(context->shared_counter, &start_index,
                                         end_index));

  return (chunk_t){start_index, end_index, 0};
}

chunk_t schedule_next_chunk(schedule_context_t *context,
                            schedule_policy_t policy) {
  if (!has_basic_context(context)) {
    return done_chunk();
  }

  if (policy >= SCHEDULE_MAX || !schedule_functions[policy]) {
    return done_chunk();
  }

  return schedule_functions[policy](context);
}

/* ------------------------------------------------------------------ */
/* schedule_execute: single-thread chunk loop with cancellation        */
/* ------------------------------------------------------------------ */

int schedule_execute(schedule_context_t *context, schedule_policy_t policy,
                     schedule_chunk_callback_t callback, void *user_data) {
  if (context == NULL || callback == NULL) {
    return -1;
  }

  while (!is_cancelled(context)) {
    chunk_t chunk = schedule_next_chunk(context, policy);
    if (chunk.done) {
      break;
    }
    callback(chunk, user_data);
  }

  return 0;
}

/* ------------------------------------------------------------------ */
/* schedule_parallel_for: one-shot thread creation and join            */
/* ------------------------------------------------------------------ */

typedef struct {
  schedule_context_t sched;
  schedule_policy_t policy;
  schedule_parallel_chunk_callback_t callback;
  void *user_data;
  atomic_int had_error;
} schedule_parallel_worker_t;

static void schedule_parallel_chunk_adapter(chunk_t chunk, void *user_data) {
  schedule_parallel_worker_t *worker = (schedule_parallel_worker_t *)user_data;
  int err = worker->callback(chunk, worker->sched.thread_id, worker->user_data);
  if (err != 0) {
    atomic_store(&worker->had_error, 1);
    if (worker->sched.cancel != NULL) {
      atomic_store(worker->sched.cancel, 1);
    }
  }
}

static void *schedule_parallel_worker_run(void *arg) {
  schedule_parallel_worker_t *worker = (schedule_parallel_worker_t *)arg;

  if (schedule_execute(&worker->sched, worker->policy,
                       schedule_parallel_chunk_adapter, worker) != 0) {
    atomic_store(&worker->had_error, 1);
  }

  return NULL;
}

int schedule_parallel_for(long total_iterations, int num_threads,
                          long chunk_size, schedule_policy_t policy,
                          schedule_parallel_chunk_callback_t callback,
                          void *user_data) {
  pthread_t *threads = NULL;
  schedule_parallel_worker_t *workers = NULL;
  padded_atomic_long_t shared_counter;
  atomic_int cancel;
  int created_threads = 0;
  int status = 0;

  atomic_init(&shared_counter.value, 0);
  atomic_init(&cancel, 0);

  if (total_iterations < 0 || num_threads <= 0 || callback == NULL ||
      policy < 0 || policy >= SCHEDULE_MAX) {
    return -1;
  }

  chunk_size = normalise_chunk_size(policy, chunk_size);

  threads = (pthread_t *)malloc((size_t)num_threads * sizeof(*threads));
  workers = (schedule_parallel_worker_t *)malloc((size_t)num_threads *
                                                 sizeof(*workers));
  if (threads == NULL || workers == NULL) {
    free(threads);
    free(workers);
    return -1;
  }

  for (int t = 0; t < num_threads; ++t) {
    workers[t].sched.thread_id = t;
    workers[t].sched.num_threads = num_threads;
    workers[t].sched.total_iterations = total_iterations;
    workers[t].sched.chunk_size = chunk_size;
    workers[t].sched.shared_counter =
        (policy == SCHEDULE_STATIC) ? NULL : &shared_counter.value;
    workers[t].sched.local_state = 0;
    workers[t].sched.cancel = &cancel;
    workers[t].policy = policy;
    workers[t].callback = callback;
    workers[t].user_data = user_data;
    atomic_init(&workers[t].had_error, 0);

    if (pthread_create(&threads[t], NULL, schedule_parallel_worker_run,
                       &workers[t]) != 0) {
      /* Signal already-running threads to stop early. */
      atomic_store(&cancel, 1);
      status = -1;
      break;
    }
    created_threads++;
  }

  for (int t = 0; t < created_threads; ++t) {
    pthread_join(threads[t], NULL);
    if (atomic_load(&workers[t].had_error)) {
      status = -1;
    }
  }

  free(threads);
  free(workers);

  return status;
}

/* ------------------------------------------------------------------ */
/* schedule_parallel_reduce_long: one-shot reduction helper            */
/* ------------------------------------------------------------------ */

typedef struct {
  schedule_parallel_chunk_reduce_long_t callback;
  void *user_data;
  padded_long_t *partial_sums;
} schedule_parallel_reduce_long_ctx_t;

static int schedule_parallel_reduce_long_adapter(chunk_t chunk, int thread_id,
                                                 void *user_data) {
  schedule_parallel_reduce_long_ctx_t *ctx =
      (schedule_parallel_reduce_long_ctx_t *)user_data;
  int error = 0;
  long result = ctx->callback(chunk, thread_id, ctx->user_data, &error);
  if (error != 0)
    return -1;
  ctx->partial_sums[thread_id].value += result;
  return 0;
}

int schedule_parallel_reduce_long(
    long total_iterations, int num_threads, long chunk_size,
    schedule_policy_t policy, schedule_parallel_chunk_reduce_long_t callback,
    void *user_data, long *out_result) {
  padded_long_t *partial_sums = NULL;
  schedule_parallel_reduce_long_ctx_t ctx;
  long total = 0;

  if (callback == NULL || out_result == NULL || num_threads <= 0) {
    return -1;
  }

  partial_sums =
      (padded_long_t *)calloc((size_t)num_threads, sizeof(*partial_sums));
  if (partial_sums == NULL) {
    return -1;
  }

  ctx.callback = callback;
  ctx.user_data = user_data;
  ctx.partial_sums = partial_sums;

  if (schedule_parallel_for(total_iterations, num_threads, chunk_size, policy,
                            schedule_parallel_reduce_long_adapter, &ctx) != 0) {
    free(partial_sums);
    return -1;
  }

  for (int t = 0; t < num_threads; ++t) {
    total += partial_sums[t].value;
  }

  *out_result = total;
  free(partial_sums);
  return 0;
}

/* Forward declaration: schedule_parallel_reduce_long_pool delegates to
 * schedule_parallel_for_pool via this adapter, which is defined above. */
static int schedule_parallel_reduce_long_adapter(chunk_t chunk, int thread_id,
                                                 void *user_data);

/* ------------------------------------------------------------------ */
/* Thread pool                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
  schedule_thread_pool_t *pool;
  int thread_id;
} pool_worker_arg_t;

struct schedule_thread_pool {
  pthread_t *threads;
  pool_worker_arg_t *args;
  int num_threads;

  pthread_mutex_t mutex;
  pthread_cond_t work_available;
  pthread_cond_t work_done;

  /* Current task (set by submitter, read by workers) */
  schedule_policy_t policy;
  schedule_parallel_chunk_callback_t callback;
  void *user_data;
  long total_iterations;
  long chunk_size;

  /* Padded shared counter for dynamic/guided scheduling. */
  padded_atomic_long_t shared_counter;

  /* Cancellation flag visible to all workers. */
  atomic_int cancel;

  int active_workers;  /* threads currently executing chunks       */
  int task_generation; /* bumped each new submission               */
  int shutdown;        /* set to 1 to terminate the pool           */
  int had_error;       /* sticky error flag for the last dispatch  */

  /* Pre-allocated scratch buffer for reduce operations. */
  padded_long_t *reduce_scratch;
};

static void *pool_worker_run(void *arg) {
  pool_worker_arg_t *warg = (pool_worker_arg_t *)arg;
  schedule_thread_pool_t *pool = warg->pool;
  int tid = warg->thread_id;
  int last_generation = 0;

  for (;;) {
    /* Wait for a new task generation or shutdown. */
    pthread_mutex_lock(&pool->mutex);
    while (pool->task_generation == last_generation && !pool->shutdown) {
      pthread_cond_wait(&pool->work_available, &pool->mutex);
    }
    if (pool->shutdown) {
      pthread_mutex_unlock(&pool->mutex);
      break;
    }
    last_generation = pool->task_generation;

    /* Snapshot task parameters while holding the lock. */
    schedule_context_t sched;
    sched.thread_id = tid;
    sched.num_threads = pool->num_threads;
    sched.total_iterations = pool->total_iterations;
    sched.chunk_size = pool->chunk_size;
    sched.shared_counter =
        (pool->policy == SCHEDULE_STATIC) ? NULL : &pool->shared_counter.value;
    sched.local_state = 0;
    sched.cancel = &pool->cancel;

    schedule_policy_t policy = pool->policy;
    schedule_parallel_chunk_callback_t callback = pool->callback;
    void *user_data = pool->user_data;
    /* Release the lock before executing chunks so the submitter thread is not
     * blocked and other workers can concurrently signal completion. */
    pthread_mutex_unlock(&pool->mutex);

    /* Execute assigned chunks without holding the pool mutex. */
    int local_error = 0;
    while (!is_cancelled(&sched)) {
      chunk_t chunk = schedule_next_chunk(&sched, policy);
      if (chunk.done)
        break;
      if (callback(chunk, tid, user_data) != 0) {
        local_error = 1;
        if (sched.cancel != NULL)
          atomic_store(sched.cancel, 1);
        break;
      }
    }

    /* Publish completion for this worker. */
    pthread_mutex_lock(&pool->mutex);
    if (local_error)
      pool->had_error = 1;
    pool->active_workers--;
    if (pool->active_workers == 0) {
      pthread_cond_signal(&pool->work_done);
    }
    pthread_mutex_unlock(&pool->mutex);
  }
  return NULL;
}

schedule_thread_pool_t *schedule_thread_pool_create(int num_threads) {
  if (num_threads <= 0)
    return NULL;

  schedule_thread_pool_t *pool =
      (schedule_thread_pool_t *)calloc(1, sizeof(*pool));
  if (!pool)
    return NULL;

  pool->num_threads = num_threads;
  if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
    free(pool);
    return NULL;
  }
  if (pthread_cond_init(&pool->work_available, NULL) != 0) {
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
    return NULL;
  }
  if (pthread_cond_init(&pool->work_done, NULL) != 0) {
    pthread_cond_destroy(&pool->work_available);
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
    return NULL;
  }
  atomic_init(&pool->shared_counter.value, 0);
  atomic_init(&pool->cancel, 0);

  pool->threads = (pthread_t *)malloc((size_t)num_threads * sizeof(pthread_t));
  pool->args = (pool_worker_arg_t *)malloc((size_t)num_threads *
                                           sizeof(pool_worker_arg_t));
  pool->reduce_scratch =
      (padded_long_t *)calloc((size_t)num_threads, sizeof(padded_long_t));
  if (!pool->threads || !pool->args || !pool->reduce_scratch) {
    free(pool->threads);
    free(pool->args);
    free(pool->reduce_scratch);
    pthread_cond_destroy(&pool->work_done);
    pthread_cond_destroy(&pool->work_available);
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
    return NULL;
  }

  int created = 0;
  for (int t = 0; t < num_threads; ++t) {
    pool->args[t].pool = pool;
    pool->args[t].thread_id = t;
    if (pthread_create(&pool->threads[t], NULL, pool_worker_run,
                       &pool->args[t]) != 0) {
      pool->shutdown = 1;
      pthread_cond_broadcast(&pool->work_available);
      for (int j = 0; j < created; ++j)
        pthread_join(pool->threads[j], NULL);
      pthread_cond_destroy(&pool->work_done);
      pthread_cond_destroy(&pool->work_available);
      pthread_mutex_destroy(&pool->mutex);
      free(pool->threads);
      free(pool->args);
      free(pool->reduce_scratch);
      free(pool);
      return NULL;
    }
    created++;
  }

  return pool;
}

void schedule_thread_pool_destroy(schedule_thread_pool_t *pool) {
  if (!pool)
    return;

  pthread_mutex_lock(&pool->mutex);
  pool->shutdown = 1;
  atomic_store(&pool->cancel,
               1); /* cancel in-flight work for faster shutdown */
  pthread_cond_broadcast(&pool->work_available);
  pthread_mutex_unlock(&pool->mutex);

  for (int t = 0; t < pool->num_threads; ++t) {
    pthread_join(pool->threads[t], NULL);
  }

  pthread_mutex_destroy(&pool->mutex);
  pthread_cond_destroy(&pool->work_available);
  pthread_cond_destroy(&pool->work_done);
  free(pool->threads);
  free(pool->args);
  free(pool->reduce_scratch);
  free(pool);
}

int schedule_parallel_for_pool(schedule_thread_pool_t *pool,
                               long total_iterations, long chunk_size,
                               schedule_policy_t policy,
                               schedule_parallel_chunk_callback_t callback,
                               void *user_data) {
  if (!pool || !callback || policy < 0 || policy >= SCHEDULE_MAX ||
      total_iterations < 0) {
    return -1;
  }

  chunk_size = normalise_chunk_size(policy, chunk_size);

  pthread_mutex_lock(&pool->mutex);

  pool->policy = policy;
  pool->callback = callback;
  pool->user_data = user_data;
  pool->total_iterations = total_iterations;
  pool->chunk_size = chunk_size;
  pool->active_workers = pool->num_threads;
  pool->had_error = 0;
  atomic_store(&pool->shared_counter.value, 0);
  atomic_store(&pool->cancel, 0);
  pool->task_generation++;

  pthread_cond_broadcast(&pool->work_available);

  /* pthread_cond_wait atomically releases pool->mutex while sleeping, so
   * workers can acquire it to decrement active_workers and signal work_done.
   * The mutex is re-acquired before returning from each wait call. */
  while (pool->active_workers > 0) {
    pthread_cond_wait(&pool->work_done, &pool->mutex);
  }

  int status = pool->had_error ? -1 : 0;
  pthread_mutex_unlock(&pool->mutex);

  return status;
}

int schedule_parallel_reduce_long_pool(
    schedule_thread_pool_t *pool, long total_iterations, long chunk_size,
    schedule_policy_t policy, schedule_parallel_chunk_reduce_long_t callback,
    void *user_data, long *out_result) {
  if (!pool || !callback || !out_result || total_iterations < 0 || policy < 0 ||
      policy >= SCHEDULE_MAX)
    return -1;

  /* Zero the pre-allocated scratch buffer. */
  memset(pool->reduce_scratch, 0,
         (size_t)pool->num_threads * sizeof(padded_long_t));

  schedule_parallel_reduce_long_ctx_t ctx;
  ctx.callback = callback;
  ctx.user_data = user_data;
  ctx.partial_sums = pool->reduce_scratch;

  if (schedule_parallel_for_pool(pool, total_iterations, chunk_size, policy,
                                 schedule_parallel_reduce_long_adapter,
                                 &ctx) != 0) {
    return -1;
  }

  long total = 0;
  for (int t = 0; t < pool->num_threads; ++t) {
    total += pool->reduce_scratch[t].value;
  }
  *out_result = total;
  return 0;
}
