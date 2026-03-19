#include "../include/pthread_schedule.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const int *array;
    long *counts_per_thread;
} count_even_callback_ctx_t;

static int is_even(int value) {
    return (value % 2) == 0;
}

static void count_chunk(const int *array, chunk_t chunk, long *accum) {
    for (long i = chunk.start_index; i < chunk.end_index; ++i) {
        if (is_even(array[i])) {
            (*accum)++;
        }
    }
}

static void count_even_parallel_callback(chunk_t chunk, int thread_id, void *user_data) {
    count_even_callback_ctx_t *ctx = (count_even_callback_ctx_t *)user_data;
    count_chunk(ctx->array, chunk, &ctx->counts_per_thread[thread_id]);
}

static long run_policy(const int *array,
                       long total_iterations,
                       int num_threads,
                       long chunk_size,
                       schedule_policy_t policy) {
    long *counts_per_thread = NULL;
    count_even_callback_ctx_t callback_ctx;
    long total_even_count = 0;

    counts_per_thread = (long *)calloc((size_t)num_threads, sizeof(*counts_per_thread));
    if (counts_per_thread == NULL) {
        fprintf(stderr, "Allocation failed.\n");
        return -1;
    }

    callback_ctx.array = array;
    callback_ctx.counts_per_thread = counts_per_thread;
    if (schedule_parallel_for(total_iterations,
                              num_threads,
                              chunk_size,
                              policy,
                              count_even_parallel_callback,
                              &callback_ctx) != 0) {
        fprintf(stderr, "schedule_parallel_for failed.\n");
        free(counts_per_thread);
        return -1;
    }

    for (int t = 0; t < num_threads; ++t) {
        total_even_count += counts_per_thread[t];
    }
    free(counts_per_thread);

    return total_even_count;
}

static const char *policy_name(schedule_policy_t policy) {
    switch (policy) {
        case SCHEDULE_STATIC:
            return "static";
        case SCHEDULE_DYNAMIC:
            return "dynamic";
        case SCHEDULE_GUIDED:
            return "guided";
        default:
            return "unknown";
    }
}

int main(void) {
    int data[] = {7, 2, 4, 11, 18, 21, 26, 9, 30, 33, 40, 1};
    long n = (long)(sizeof(data) / sizeof(data[0]));

    int num_threads = 4;
    long chunk_size = 2;

    schedule_policy_t policies[] = {
        SCHEDULE_STATIC,
        SCHEDULE_DYNAMIC,
        SCHEDULE_GUIDED
    };

    for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i) {
        schedule_policy_t policy = policies[i];
        long count = run_policy(data, n, num_threads, chunk_size, policy);
        if (count < 0) {
            fprintf(stderr, "%s scheduling failed.\n", policy_name(policy));
            return EXIT_FAILURE;
        }
        printf("Policy=%s, even_count=%ld\n", policy_name(policy), count);
    }

    return EXIT_SUCCESS;
}
