cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(tp-tests "${SOURCES}")

target_link_libraries(tp-tests PUBLIC
    clean-core
    typed-geometry
    texture-processor
)
