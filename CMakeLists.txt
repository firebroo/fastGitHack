cmake_minimum_required(VERSION 3.0.2)
project(C)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -lz -lpthread -lcurl -std=c++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -lz -lpthread -lcurl -std=gnu99")

set(SOURCE_FILES githack.c thpool.c http.c)
add_executable(githack ${SOURCE_FILES})
