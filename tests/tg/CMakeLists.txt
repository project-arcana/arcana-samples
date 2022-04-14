cmake_minimum_required(VERSION 3.11)

file(GLOB_RECURSE SOURCES
    "*.cc"
    "*.hh"
)

add_arcana_test(tg-tests "${SOURCES}")

target_link_libraries(tg-tests PUBLIC
    typed-geometry
    rich-log
)
