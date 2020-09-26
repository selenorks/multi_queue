cmake_minimum_required(VERSION 3.10)

project(queues_lib)


file(GLOB
        SOURCES
        *.cxx
        *.h
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC .)