cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(res-tests "${SOURCES}")

target_link_libraries(res-tests PUBLIC
    rich-log
    resource-system
)
