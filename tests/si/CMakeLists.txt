cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(si-tests "${SOURCES}")

target_link_libraries(si-tests PUBLIC
    structured-interface
    rich-log
)
