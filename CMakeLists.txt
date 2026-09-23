cmake_minimum_required(VERSION 3.22)

# the version is written once, in core/version.h, and read back from there
file(STRINGS include/tda/core/version.h TDA_VERSION_DEFINES
     REGEX "^#define TDA_VERSION_(MAJOR|MINOR|PATCH) +[0-9]+$")
foreach (part MAJOR MINOR PATCH)
    string(REGEX MATCH "TDA_VERSION_${part} +([0-9]+)" _ "${TDA_VERSION_DEFINES}")
    set(TDA_VERSION_${part} ${CMAKE_MATCH_1})
endforeach ()

project(terse-dsa
        VERSION ${TDA_VERSION_MAJOR}.${TDA_VERSION_MINOR}.${TDA_VERSION_PATCH}
        DESCRIPTION "Terse data structures and algorithms in C23"
        LANGUAGES C
)

if (PROJECT_IS_TOP_LEVEL)
    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif ()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_C_EXTENSIONS OFF)
endif ()

set(TDA_C_STANDARD 23)

option(TDA_BUILD_SHARED "Build tda as a shared library" ${PROJECT_IS_TOP_LEVEL})
option(TDA_BUILD_TEST "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(TDA_BUILD_EXAMPLE "Build the examples the reference quotes" ${PROJECT_IS_TOP_LEVEL})
option(TDA_BUILD_BENCH "Build the benchmarks" OFF)
option(TDA_HARDENED "Keep the bounds and emptiness checks in release builds" OFF)

set(TDA_WARNINGS
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)

set(TDA_ALGO_SOURCES
        src/algo/compare.c
        src/algo/copy.c
        src/algo/fill.c
        src/algo/fold.c
        src/algo/heap.c
        src/algo/merge.c
        src/algo/modify.c
        src/algo/permute.c
        src/algo/search.c
        src/algo/set.c
        src/algo/sort.c
        src/algo/transform.c
)

set(TDA_ALLOC_SOURCES
        src/alloc/aligned.c
        src/alloc/alloc.c
        src/alloc/arena.c
        src/alloc/default.c
        src/alloc/log.c
        src/alloc/pool.c
)

set(TDA_CORE_SOURCES
        src/core/cmp.c
        src/core/hash.c
        src/core/print.c
        src/core/rng.c
        src/core/status.c
)

set(TDA_DS_SOURCES
        src/ds/arr.c
        src/ds/bitset.c
        src/ds/deque.c
        src/ds/hmap.c
        src/ds/hset.c
        src/ds/list.c
        src/ds/pqueue.c
        src/ds/queue.c
        src/ds/stack.c
        src/ds/vec.c
)

set(TDA_SOURCES
        ${TDA_ALGO_SOURCES}
        ${TDA_ALLOC_SOURCES}
        ${TDA_CORE_SOURCES}
        ${TDA_DS_SOURCES}
)

if (TDA_BUILD_SHARED)
    set(TDA_LIBRARY_TYPE SHARED)
else ()
    set(TDA_LIBRARY_TYPE STATIC)
endif ()

add_library(tda ${TDA_LIBRARY_TYPE} ${TDA_SOURCES})
add_library(tda::tda ALIAS tda)

target_include_directories(tda
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_compile_features(tda PUBLIC c_std_${TDA_C_STANDARD})
target_compile_options(tda PRIVATE ${TDA_WARNINGS})

if (UNIX)
    target_link_libraries(tda PRIVATE m)
endif ()

# PUBLIC: span.h checks inline, in the caller's own translation unit
target_compile_definitions(tda
        PRIVATE TDA_BUILD
        PUBLIC $<$<NOT:$<BOOL:${TDA_BUILD_SHARED}>>:TDA_STATIC>
        PUBLIC $<$<BOOL:${TDA_HARDENED}>:TDA_HARDENED>
)

set_target_properties(tda PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
        C_EXTENSIONS OFF
        C_VISIBILITY_PRESET hidden
)

include(CheckIPOSupported)
check_ipo_supported(RESULT TDA_IPO_SUPPORTED OUTPUT TDA_IPO_ERROR)
if (TDA_IPO_SUPPORTED)
    set_target_properties(tda PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
    )
else ()
    message(STATUS "  LTO unavailable : ${TDA_IPO_ERROR}")
endif ()

if (TDA_BUILD_TEST OR TDA_BUILD_EXAMPLE)
    enable_testing()
endif ()

if (TDA_BUILD_TEST)
    set(TDA_UNITY_DIR ${PROJECT_SOURCE_DIR}/thirdparty/Unity-2.7.0)
    add_subdirectory(${TDA_UNITY_DIR} EXCLUDE_FROM_ALL)
    target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_INCLUDE_FLOAT)

    add_subdirectory(test)
endif ()

if (TDA_BUILD_EXAMPLE)
    add_subdirectory(example)
endif ()

if (TDA_BUILD_BENCH)
    add_subdirectory(bench)
endif ()

if (PROJECT_IS_TOP_LEVEL)
    string(TOLOWER ${TDA_LIBRARY_TYPE} TDA_LIBRARY_TYPE_LOWER)
    message(STATUS "")
    message(STATUS "terse-dsa configuration:")
    message(STATUS "  version        : ${PROJECT_VERSION}")
    message(STATUS "  build type     : ${CMAKE_BUILD_TYPE}")
    message(STATUS "  library        : ${TDA_LIBRARY_TYPE_LOWER}")
    message(STATUS "  compiler       : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
    message(STATUS "  C standard     : C${TDA_C_STANDARD}")
    message(STATUS "  hardened       : ${TDA_HARDENED}")
    message(STATUS "  tests          : ${TDA_BUILD_TEST}")
    message(STATUS "  examples       : ${TDA_BUILD_EXAMPLE}")
    message(STATUS "  benchmarks     : ${TDA_BUILD_BENCH}")
    message(STATUS "")
endif ()
