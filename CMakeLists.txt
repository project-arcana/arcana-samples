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

## External

# unit tests
add_subdirectory(extern/doctest)


## Core

# std replacement
add_subdirectory(extern/clean-core)

# math library
add_subdirectory(extern/typed-geometry)

# tracing and profiling
add_subdirectory(extern/ctracer)

# reflection and introspection
add_subdirectory(extern/reflector)

# multithreading
add_subdirectory(extern/task-dispatcher)


## Main

# half-edge meshes
add_subdirectory(extern/polymesh)

# TODO: other libs


# ===============================================
# Compile flags

set(COMMON_COMPILE_FLAGS "")

if (MSVC)
    list(APPEND COMMON_COMPILE_FLAGS
        /MP
    )
else()
    list(APPEND COMMON_COMPILE_FLAGS
        -Wall
        -Wno-unused-variable
    )
endif()


# ===============================================
# Tests and samples

function(add_arcana_test TEST_NAME SOURCES)
    # proper folder structure in VS
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

    # create target
    add_executable(${TEST_NAME} ${SOURCES})

    # set compile flags
    target_compile_options(${TEST_NAME} PUBLIC ${COMMON_COMPILE_FLAGS})

    # make doctest available
    target_link_libraries(${TEST_NAME} PUBLIC doctest)

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
    doctest
    reflector
)
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Extern")
endforeach()
