cmake_minimum_required(VERSION 3.0.2)
project(C)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -lz -std=c++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -lz -std=gnu99")

set(SOURCE_FILES githack.c http.c)
add_executable(githack ${SOURCE_FILES})
