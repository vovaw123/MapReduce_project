cmake_minimum_required(VERSION 3.16)
project(mapreduce_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MAPREDUCE_BUILD_DEMO "Build demo executable" ON)
option(MAPREDUCE_BUILD_TESTS "Build tests" ON)

add_library(mapreduce_lib INTERFACE)
target_include_directories(mapreduce_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MAPREDUCE_BUILD_DEMO)
	add_executable(mapreduce_demo examples/demo_main.cpp)
	target_link_libraries(mapreduce_demo PRIVATE mapreduce_lib)
endif()

if(MAPREDUCE_BUILD_TESTS)
	add_executable(mapreduce_tests tests/test_mapreduce.cpp)
	target_link_libraries(mapreduce_tests PRIVATE mapreduce_lib)

	enable_testing()
	add_test(NAME mapreduce_tests COMMAND mapreduce_tests)
endif()
