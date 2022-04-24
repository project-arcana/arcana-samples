cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(phi-tests "${SOURCES}")

target_link_libraries(phi-tests PUBLIC
    clean-core
    task-dispatcher
    typed-geometry
    dxc-wrapper
    phantasm-hardware-interface
    phantasm-renderer
    arcana-incubator
)

set_property(TARGET phi-tests PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

