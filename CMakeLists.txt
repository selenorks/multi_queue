cmake_minimum_required(VERSION 3.10)

# set the project name
project(Test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_subdirectory(benchmark)
add_subdirectory(src)
add_subdirectory(tests)