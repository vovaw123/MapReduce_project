cmake_minimum_required(VERSION 3.16)
project(mapreduce_cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MAPREDUCE_BUILD_DEMO  "Build demo executable"  ON)
option(MAPREDUCE_BUILD_TESTS "Build tests"             ON)
option(MAPREDUCE_USE_MPI     "Build MPI demo"          OFF)

# ---------------------------------------------------------------------------
# Header-only library target
# ---------------------------------------------------------------------------
add_library(mapreduce_lib INTERFACE)
target_include_directories(mapreduce_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Optional: MPI
# ---------------------------------------------------------------------------
if(MAPREDUCE_USE_MPI)
    # MS-MPI on Windows commonly exposes C API (mpi.h) but not MPI_CXX target.
    # Our demo uses C MPI API from C++ code, so MPI C component is sufficient.
    if(WIN32)
        find_package(MPI QUIET COMPONENTS C)
        if(MPI_C_FOUND)
            set(MAPREDUCE_MPI_TARGET MPI::MPI_C)
        else()
            set(_msmpi_include_hints)
            set(_msmpi_lib_hints)

            if(DEFINED ENV{MSMPI_INC})
                list(APPEND _msmpi_include_hints "$ENV{MSMPI_INC}")
            endif()
            if(DEFINED ENV{MSMPI_LIB64})
                list(APPEND _msmpi_lib_hints "$ENV{MSMPI_LIB64}")
            endif()

            list(APPEND _msmpi_include_hints
                "C:/Program Files/Microsoft MPI/Include"
                "C:/Program Files (x86)/Microsoft SDKs/MPI/Include"
            )
            list(APPEND _msmpi_lib_hints
                "C:/Program Files/Microsoft MPI/Lib/x64"
                "C:/Program Files (x86)/Microsoft SDKs/MPI/Lib/x64"
            )

            find_path(MSMPI_INCLUDE_DIR NAMES mpi.h HINTS ${_msmpi_include_hints})
            find_library(MSMPI_LIBRARY NAMES msmpi HINTS ${_msmpi_lib_hints})

            if(MSMPI_INCLUDE_DIR AND MSMPI_LIBRARY)
                add_library(MAPREDUCE_MS_MPI INTERFACE)
                target_include_directories(MAPREDUCE_MS_MPI INTERFACE "${MSMPI_INCLUDE_DIR}")
                target_link_libraries(MAPREDUCE_MS_MPI INTERFACE "${MSMPI_LIBRARY}")
                set(MAPREDUCE_MPI_TARGET MAPREDUCE_MS_MPI)
                message(STATUS "Using MS-MPI fallback include=${MSMPI_INCLUDE_DIR}")
                message(STATUS "Using MS-MPI fallback lib=${MSMPI_LIBRARY}")
            else()
                message(FATAL_ERROR
                    "Could not find MPI_C via FindMPI and could not locate MS-MPI fallback.\n"
                    "Set MSMPI_INC and MSMPI_LIB64, or install MS-MPI to standard path."
                )
            endif()
        endif()
    else()
        find_package(MPI REQUIRED COMPONENTS CXX)
        set(MAPREDUCE_MPI_TARGET MPI::MPI_CXX)
    endif()
endif()

# ---------------------------------------------------------------------------
# Demo – classify_lines + word_count (serial / threaded)
# ---------------------------------------------------------------------------
if(MAPREDUCE_BUILD_DEMO)
    add_executable(mapreduce_demo examples/demo_main.cpp)
    target_link_libraries(mapreduce_demo PRIVATE mapreduce_lib)

    # MPI demo – word_count across multiple machines
    if(MAPREDUCE_USE_MPI)
        add_executable(mapreduce_mpi_demo examples/word_count_mpi.cpp)
        target_compile_definitions(mapreduce_mpi_demo PRIVATE MAPREDUCE_USE_MPI)
        target_link_libraries(mapreduce_mpi_demo PRIVATE mapreduce_lib ${MAPREDUCE_MPI_TARGET})
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MAPREDUCE_BUILD_TESTS)
    add_executable(mapreduce_tests tests/test_mapreduce.cpp)
    target_link_libraries(mapreduce_tests PRIVATE mapreduce_lib)

    enable_testing()
    add_test(NAME mapreduce_tests COMMAND mapreduce_tests)
endif()

