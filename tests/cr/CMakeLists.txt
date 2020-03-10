cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(cr-tests "${SOURCES}")

target_link_libraries(cr-tests PUBLIC
    clean-core
    clean-ranges
)
