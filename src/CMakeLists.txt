cmake_minimum_required(VERSION 3.10)

project(multi_queue_lib)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(
        ${PROJECT_NAME}
        INTERFACE
)

target_include_directories(
        ${PROJECT_NAME}
        INTERFACE
        .
)

target_link_libraries(
        ${PROJECT_NAME}
        INTERFACE
        Threads::Threads
)
