cmake_minimum_required(VERSION 3.10)
project(pthread_schedule C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(pthread_schedule STATIC src/pthread_schedule.c)
target_include_directories(pthread_schedule PUBLIC include)
target_link_libraries(pthread_schedule PRIVATE Threads::Threads)

add_executable(count_even_example examples/count_even_example.c)
target_link_libraries(count_even_example PRIVATE pthread_schedule Threads::Threads)
