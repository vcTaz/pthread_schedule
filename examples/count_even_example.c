#include "../include/pthread_schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long count_even_reduce_callback(chunk_t chunk, int thread_id,
                                       void *user_data, int *error) {
  (void)thread_id;
  (void)error;
  const int *array = (const int *)user_data;
  long local_count = 0;
  for (long i = chunk.start_index; i < chunk.end_index; ++i) {
    if (array[i] % 2 == 0) {
      local_count++;
    }
  }
  return local_count;
}

static long run_policy(const int *array, long total_iterations, int num_threads,
                       long chunk_size, schedule_policy_t policy) {
  long total_even_count = 0;

  if (schedule_parallel_reduce_long(total_iterations, num_threads, chunk_size,
                                    policy, count_even_reduce_callback,
                                    (void *)array, &total_even_count) != 0) {
    fprintf(stderr, "schedule_parallel_reduce_long failed.\n");
    return -1;
  }

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
  long n = 10000000; // 10 Million elements
  int *data = (int *)malloc(n * sizeof(int));
  if (data == NULL) {
    fprintf(stderr, "Failed to allocate data.\n");
    return EXIT_FAILURE;
  }

  // Generate some random integers
  srand(42);
  for (long i = 0; i < n; ++i) {
    data[i] = rand();
  }

  int num_threads = 4;
  long chunk_size = 1000; // Sensible chunk size for big workloads

  schedule_policy_t policies[] = {SCHEDULE_STATIC, SCHEDULE_DYNAMIC,
                                  SCHEDULE_GUIDED};

  for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i) {
    schedule_policy_t policy = policies[i];

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    long count = run_policy(data, n, num_threads, chunk_size, policy);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed =
        (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    if (count < 0) {
      fprintf(stderr, "%s scheduling failed.\n", policy_name(policy));
      free(data);
      return EXIT_FAILURE;
    }
    printf("Policy=%-10s | count=%-8ld | time=%.4f seconds\n",
           policy_name(policy), count, elapsed);
  }

  free(data);
  return EXIT_SUCCESS;
}
