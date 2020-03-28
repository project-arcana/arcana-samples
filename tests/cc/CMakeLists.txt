cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(cc-tests "${SOURCES}")

target_link_libraries(cc-tests PUBLIC
    clean-core
    clean-ranges
    typed-geometry
    rich-log
    ctracer
)
