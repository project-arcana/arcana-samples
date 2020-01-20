cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(ct-tests "${SOURCES}")

target_link_libraries(ct-tests PUBLIC
    ctracer
    rich-log
)
