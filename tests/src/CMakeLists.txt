cmake_minimum_required(VERSION 3.10)

# set the project name
project(tests)

find_package(GTest REQUIRED)

#file(GLOB src/*.cxx)
include(GoogleTest)
add_executable(${PROJECT_NAME} unit_test.cxx base_test.h)
target_link_libraries(${PROJECT_NAME} GTest::Main queues_lib benchmark::benchmark)

add_executable(${PROJECT_NAME}_bench bench.cxx)
target_link_libraries(${PROJECT_NAME}_bench queues_lib benchmark::benchmark)