cmake_minimum_required(VERSION 3.6.1)
project(CacheFS2)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -std=c++11 -DNDEBUG")

set(SOURCE_FILES TEST.cpp CacheFS.h CacheFS.cpp Block.h Block.cpp debug.h)
add_executable(CacheFS2 ${SOURCE_FILES})