#include "../include/pthread_schedule.h"

static chunk_t (*const schedule_functions[])(schedule_context_t *) = {
    [SCHEDULE_STATIC]  = schedule_static,
    [SCHEDULE_DYNAMIC] = schedule_dynamic,
    [SCHEDULE_GUIDED]  = schedule_guided
};

static chunk_t done_chunk(void) {
    return (chunk_t){0, 0, 1};
}

static int has_basic_context(const schedule_context_t *context) {
    return context != NULL &&
           context->num_threads > 0 &&
           context->thread_id >= 0 &&
           context->thread_id < context->num_threads &&
           context->total_iterations >= 0;
}

typedef struct {
    schedule_context_t sched;
    schedule_policy_t policy;
    schedule_parallel_chunk_callback_t callback;
    void *user_data;
    int had_error;
} schedule_parallel_worker_t;

static void schedule_parallel_chunk_adapter(chunk_t chunk, void *user_data) {
    schedule_parallel_worker_t *worker = (schedule_parallel_worker_t *)user_data;
    worker->callback(chunk, worker->sched.thread_id, worker->user_data);
}

static void *schedule_parallel_worker_run(void *arg) {
    schedule_parallel_worker_t *worker = (schedule_parallel_worker_t *)arg;

    if (schedule_execute(&worker->sched,
                         worker->policy,
                         schedule_parallel_chunk_adapter,
                         worker) != 0) {
        worker->had_error = 1;
    }

    return NULL;
}

chunk_t schedule_static(schedule_context_t *context) {
    if (!has_basic_context(context)) {
        return done_chunk();
    }

    if (context->local_state != 0) {
        return done_chunk();
    }
    context->local_state = 1;

    long base_chunk = context->total_iterations / context->num_threads;
    long remainder = context->total_iterations % context->num_threads;
    long start_index = context->thread_id * base_chunk +
                       ((context->thread_id < remainder) ? context->thread_id : remainder);
    long my_size = base_chunk + ((context->thread_id < remainder) ? 1 : 0);
    long end_index = start_index + my_size;

    return (chunk_t){start_index, end_index, start_index >= context->total_iterations};
}

chunk_t schedule_dynamic(schedule_context_t *context) {
    if (!has_basic_context(context) ||
        context->lock == NULL ||
        context->shared_counter == NULL ||
        context->chunk_size <= 0) {
        return done_chunk();
    }

    if (pthread_mutex_lock(context->lock) != 0) {
        return done_chunk();
    }

    long start_index = *context->shared_counter;
    if (start_index >= context->total_iterations) {
        pthread_mutex_unlock(context->lock);
        return done_chunk();
    }

    long end_index = start_index + context->chunk_size;
    if (end_index > context->total_iterations) {
        end_index = context->total_iterations;
    }
    *context->shared_counter = end_index;
    pthread_mutex_unlock(context->lock);

    return (chunk_t){start_index, end_index, 0};
}

chunk_t schedule_guided(schedule_context_t *context) {
    if (!has_basic_context(context) ||
        context->lock == NULL ||
        context->shared_counter == NULL ||
        context->chunk_size <= 0) {
        return done_chunk();
    }

    if (pthread_mutex_lock(context->lock) != 0) {
        return done_chunk();
    }

    long start_index = *context->shared_counter;
    long remaining_iterations = context->total_iterations - start_index;
    if (remaining_iterations <= 0) {
        pthread_mutex_unlock(context->lock);
        return done_chunk();
    }

    long chunk_size = (remaining_iterations + context->num_threads - 1) / context->num_threads;
    if (chunk_size < context->chunk_size) {
        chunk_size = context->chunk_size;
    }

    long end_index = start_index + chunk_size;
    if (end_index > context->total_iterations) {
        end_index = context->total_iterations;
    }
    *context->shared_counter = end_index;
    pthread_mutex_unlock(context->lock);

    return (chunk_t){start_index, end_index, 0};
}

chunk_t schedule_next_chunk(schedule_context_t *context, schedule_policy_t policy) {
    if (!has_basic_context(context)) {
        return done_chunk();
    }

    if (policy >= SCHEDULE_MAX || !schedule_functions[policy]) {
        return done_chunk();
    }

    return schedule_functions[policy](context);
}

int schedule_execute(schedule_context_t *context,
                     schedule_policy_t policy,
                     schedule_chunk_callback_t callback,
                     void *user_data) {
    if (context == NULL || callback == NULL) {
        return -1;
    }

    while (1) {
        chunk_t chunk = schedule_next_chunk(context, policy);
        if (chunk.done) {
            break;
        }
        callback(chunk, user_data);
    }

    return 0;
}

int schedule_parallel_for(long total_iterations,
                          int num_threads,
                          long chunk_size,
                          schedule_policy_t policy,
                          schedule_parallel_chunk_callback_t callback,
                          void *user_data) {
    pthread_t *threads = NULL;
    schedule_parallel_worker_t *workers = NULL;
    pthread_mutex_t lock;
    int lock_initialized = 0;
    long shared_counter = 0;
    int created_threads = 0;
    int status = 0;

    if (total_iterations < 0 ||
        num_threads <= 0 ||
        callback == NULL ||
        policy < 0 ||
        policy >= SCHEDULE_MAX) {
        return -1;
    }

    if ((policy == SCHEDULE_DYNAMIC || policy == SCHEDULE_GUIDED) && chunk_size <= 0) {
        return -1;
    }

    threads = (pthread_t *)malloc((size_t)num_threads * sizeof(*threads));
    workers = (schedule_parallel_worker_t *)malloc((size_t)num_threads * sizeof(*workers));
    if (threads == NULL || workers == NULL) {
        free(threads);
        free(workers);
        return -1;
    }

    if (policy == SCHEDULE_DYNAMIC || policy == SCHEDULE_GUIDED) {
        if (pthread_mutex_init(&lock, NULL) != 0) {
            free(threads);
            free(workers);
            return -1;
        }
        lock_initialized = 1;
    }

    for (int t = 0; t < num_threads; ++t) {
        workers[t].sched.thread_id = t;
        workers[t].sched.num_threads = num_threads;
        workers[t].sched.total_iterations = total_iterations;
        workers[t].sched.chunk_size = chunk_size;
        workers[t].sched.shared_counter =
            (policy == SCHEDULE_STATIC) ? NULL : &shared_counter;
        workers[t].sched.lock = (policy == SCHEDULE_STATIC) ? NULL : &lock;
        workers[t].sched.local_state = 0;
        workers[t].policy = policy;
        workers[t].callback = callback;
        workers[t].user_data = user_data;
        workers[t].had_error = 0;

        if (pthread_create(&threads[t], NULL, schedule_parallel_worker_run, &workers[t]) != 0) {
            status = -1;
            break;
        }
        created_threads++;
    }

    for (int t = 0; t < created_threads; ++t) {
        pthread_join(threads[t], NULL);
        if (workers[t].had_error) {
            status = -1;
        }
    }

    if (lock_initialized) {
        pthread_mutex_destroy(&lock);
    }
    free(threads);
    free(workers);

    return status;
}

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

typedef struct {
    long value;
    char padding[CACHE_LINE_SIZE - sizeof(long)];
} padded_long_t;

typedef struct {
    schedule_parallel_chunk_reduce_long_t callback;
    void *user_data;
    padded_long_t *partial_sums;
} schedule_parallel_reduce_long_ctx_t;

static void schedule_parallel_reduce_long_adapter(chunk_t chunk,
                                                  int thread_id,
                                                  void *user_data) {
    schedule_parallel_reduce_long_ctx_t *ctx =
        (schedule_parallel_reduce_long_ctx_t *)user_data;
    ctx->partial_sums[thread_id].value += ctx->callback(chunk, ctx->user_data);
}

int schedule_parallel_reduce_long(long total_iterations,
                                  int num_threads,
                                  long chunk_size,
                                  schedule_policy_t policy,
                                  schedule_parallel_chunk_reduce_long_t callback,
                                  void *user_data,
                                  long *out_result) {
    padded_long_t *partial_sums = NULL;
    schedule_parallel_reduce_long_ctx_t ctx;
    long total = 0;

    if (callback == NULL || out_result == NULL || num_threads <= 0) {
        return -1;
    }

    partial_sums = (padded_long_t *)calloc((size_t)num_threads, sizeof(*partial_sums));
    if (partial_sums == NULL) {
        return -1;
    }

    ctx.callback = callback;
    ctx.user_data = user_data;
    ctx.partial_sums = partial_sums;

    if (schedule_parallel_for(total_iterations,
                              num_threads,
                              chunk_size,
                              policy,
                              schedule_parallel_reduce_long_adapter,
                              &ctx) != 0) {
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
