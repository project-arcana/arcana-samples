cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(nx-tests "${SOURCES}")

target_link_libraries(nx-tests PUBLIC
    clean-core
    typed-geometry
)
