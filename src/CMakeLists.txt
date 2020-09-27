cmake_minimum_required(VERSION 3.10)

project(multi_queue_lib)

file(GLOB
        SOURCES
        *.cxx
        *.h
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(${PROJECT_NAME} STATIC ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC .)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)