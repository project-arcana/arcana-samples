cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(td-tests "${SOURCES}")

target_link_libraries(td-tests PUBLIC
    clean-core
    task-dispatcher
)
