cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(rf-tests "${SOURCES}")

target_link_libraries(rf-tests PUBLIC
    clean-core
    reflector
    typed-geometry
)
