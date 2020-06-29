cmake_minimum_required(VERSION 3.11)
project(Arcana)

# ===============================================
# Global settings

# require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# folders for visual studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)


# ==============================================================================
# Set bin dir
if(MSVC)
    set(BIN_DIR ${CMAKE_SOURCE_DIR}/bin)
elseif(CMAKE_BUILD_TYPE STREQUAL "")
    set(BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Default)
else()
    set(BIN_DIR ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})


# ===============================================
# Dependencies


## Core

# std replacement
add_subdirectory(extern/clean-core)

# ranges and collections
add_subdirectory(extern/clean-ranges)

# math library
add_subdirectory(extern/typed-geometry)

# tracing and profiling
add_subdirectory(extern/ctracer)

# reflection and introspection
add_subdirectory(extern/reflector)

# logging
add_subdirectory(extern/rich-log)

# multithreading
add_subdirectory(extern/task-dispatcher)

# resources
add_subdirectory(extern/resource-system)


## Main

# half-edge meshes
add_subdirectory(extern/polymesh)

# SDL2
add_subdirectory(extern/sdl2-dev)

# phantasm hardware interface
add_subdirectory(extern/phantasm-hardware-interface)

# shader compiler
set(DXCWRAP_BUILD_STANDALONE ON CACHE BOOL "" FORCE)
add_subdirectory(extern/dxc-wrapper)

# phantasm renderer
add_subdirectory(extern/phantasm-renderer)

# UI
add_subdirectory(extern/structured-interface)

# serialization
add_subdirectory(extern/babel-serializer)

# TODO: other libs


# testing, benchmarking, apps
add_subdirectory(extern/nexus)

## Incubator

# provisional code
add_subdirectory(extern/arcana-incubator)

# ===============================================
# Testing options

option(ARC_ENABLE_ASAN "if true, enables clang/MSVC address sanitizer" OFF)
option(ARC_ENABLE_MSAN "if true, enables clang/MSVC memory sanitizer" OFF)
option(ARC_ENABLE_UBSAN "if true, enables clang/MSVC undefined behaviour sanitizer" OFF)
option(ARC_ENABLE_TSAN "if true, enables clang/MSVC thread sanitizer" OFF)

if (ARC_ENABLE_ASAN AND ARC_ENABLE_TSAN)
    message(FATAL_ERROR "Can only enable one of TSan or ASan at a time")
endif()
if (ARC_ENABLE_ASAN AND ARC_ENABLE_MSAN)
    message(FATAL_ERROR "Can only enable one of ASan or MSan at a time")
endif()

option(ARC_DISABLE_EXCEPTIONS "Set compiler flags to disable exception handling" OFF)
option(ARC_DISABLE_RTTI "Set compiler flags to disable RTTI" OFF)

# ===============================================
# Compile flags

set(COMMON_COMPILER_FLAGS "")
set(COMMON_LINKER_FLAGS "")

if (MSVC)
    list(APPEND COMMON_COMPILER_FLAGS
        /MP
    )

    if (ARC_DISABLE_EXCEPTIONS)
        string(REPLACE "/EHsc" "/EHs-c- /D _HAS_EXCEPTIONS=0" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    endif()

    if (ARC_DISABLE_RTTI)
        string(REPLACE "/GR" "/GR-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    endif()
else()
    list(APPEND COMMON_COMPILER_FLAGS
        -Wall
        -Wno-unused-variable
    )

    if (ARC_DISABLE_EXCEPTIONS)
        list(APPEND COMMON_COMPILER_FLAGS -fno-exceptions)
    endif()

    if (ARC_DISABLE_RTTI)
        list(APPEND COMMON_COMPILER_FLAGS -fno-rtti)
    endif()

    if (ARC_ENABLE_ASAN OR ARC_ENABLE_TSAN OR ARC_ENABLE_MSAN OR ARC_ENABLE_UBSAN)
        list(APPEND COMMON_COMPILER_FLAGS -fno-omit-frame-pointer -g)
        list(APPEND COMMON_LINKER_FLAGS -fno-omit-frame-pointer -g)
    endif()

    if (ARC_ENABLE_ASAN)
        list(APPEND COMMON_COMPILER_FLAGS -fsanitize=address)
        list(APPEND COMMON_LINKER_FLAGS -fsanitize=address)
    endif()

    if (ARC_ENABLE_TSAN)
        list(APPEND COMMON_COMPILER_FLAGS -fsanitize=thread)
        list(APPEND COMMON_LINKER_FLAGS -fsanitize=thread)
    endif()

    if (ARC_ENABLE_MSAN)
        list(APPEND COMMON_COMPILER_FLAGS -fsanitize=memory)
        list(APPEND COMMON_LINKER_FLAGS -fsanitize=memory)
    endif()

    if (ARC_ENABLE_UBSAN)
        list(APPEND COMMON_COMPILER_FLAGS
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -fno-sanitize=alignment,vptr
        )
        list(APPEND COMMON_LINKER_FLAGS
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -fno-sanitize=alignment,vptr
        )
    endif()
endif()


# ===============================================
# Tests and samples

function(add_arcana_test TEST_NAME SOURCES)
    # create target
    add_executable(${TEST_NAME} ${SOURCES})

    # set compiler flags
    target_compile_options(${TEST_NAME} PUBLIC ${COMMON_COMPILER_FLAGS})

    # set linker flags, make nexus available
    target_link_libraries(${TEST_NAME} PUBLIC nexus ${COMMON_LINKER_FLAGS})

    # move into tests folder
    set_property(TARGET ${TEST_NAME} PROPERTY FOLDER "Tests")
endfunction()

# register samples and tests
add_subdirectory(tests)
# TODO: samples


# ===============================================
# Folder grouping

foreach(TARGET_NAME
    polymesh
    typed-geometry
    ctracer
    clean-core
    nexus
    reflector
    phantasm-renderer
    rich-log
    task-dispatcher
    structured-interface
    arcana-incubator
    dxc-wrapper
    dxc-wrap
    resource-system
    clean-ranges
    phantasm-hardware-interface
)
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Extern")
endforeach()

foreach(TARGET_NAME
    dxc-wrapper-standalone
)
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Tools")
endforeach()
