cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(rlog-tests "${SOURCES}")

target_link_libraries(rlog-tests PUBLIC
    clean-core
    rich-log
)
