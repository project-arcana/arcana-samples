cmake_minimum_required(VERSION 3.11)
project(Doctest)

add_library(doctest STATIC
    "src/doctest.cc"
    "src/doctest.hh"
)

target_include_directories(doctest PUBLIC "src")
