cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(pm-tests "${SOURCES}")

target_link_libraries(pm-tests PUBLIC
    clean-core
    typed-geometry
    polymesh
)
