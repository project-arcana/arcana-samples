cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(pr-tests "${SOURCES}")

target_link_libraries(pr-tests PUBLIC
    clean-core
    task-dispatcher
    typed-geometry
)
