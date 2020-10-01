cmake_minimum_required(VERSION 3.10)

project(unit_tests)

add_executable(
        ${PROJECT_NAME}
        unit_tests.cxx
        test_common.h
)

target_link_libraries(
        ${PROJECT_NAME}
        gtest_main
        multi_queue_lib
        benchmark::benchmark
)

add_executable(
        benchmarks
        benchmarks.cxx
        multi_queue_old.h)

target_link_libraries(
        benchmarks
        multi_queue_lib
        gtest_main
        benchmark::benchmark
)

add_test(
        NAME
            multi_queue_unit_tests
        COMMAND
            $<TARGET_FILE:unit_tests>
)
