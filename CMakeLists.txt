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

# serialization
add_subdirectory(extern/babel-serializer)


## Main

# half-edge meshes
add_subdirectory(extern/polymesh)

# SDL2
add_subdirectory(extern/sdl2-dev)

# phantasm hardware interface
add_subdirectory(extern/phantasm-hardware-interface)

# shader compiler
set(DXCW_BUILD_STANDALONE ON CACHE BOOL "")
add_subdirectory(extern/dxc-wrapper)

# texture processing
add_subdirectory(extern/texture-processor)

# phantasm renderer
add_subdirectory(extern/phantasm-renderer)

# UI
add_subdirectory(extern/structured-interface)

# TODO: other libs


# testing, benchmarking, apps
add_subdirectory(extern/nexus)

## Incubator

# provisional code
add_subdirectory(extern/arcana-incubator)

# ===============================================
# Testing options

# currently empty



# ===============================================
# Tests and samples

# include helper for testing
include(extern/clean-core/cmake/AddTest.cmake)

# register arcana tests
add_subdirectory(extern/clean-core/tests)

# register extra samples and tests
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
    babel-serializer
    texture-processor
)
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Extern")
endforeach()

foreach(TARGET_NAME
    dxc-wrapper-standalone
)
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Tools")
endforeach()
