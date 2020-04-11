FILE(GLOB_RECURSE files 
    "tests/*.cc"
    "tests/*.hh"
    "extern/*.cc"
    "extern/*.hh"
)

set(FINE TRUE)

foreach(filename ${files})
    # message(STATUS "checking ${filename}")

    file(READ ${filename} content)

    execute_process(
        COMMAND clang-format-7 ${filename} -style=file
        OUTPUT_VARIABLE formatted
    )

    if (content STREQUAL formatted)
        # fine
    else()
        message("file ${filename} is not formatted")
        set(FINE FALSE)
    endif()
endforeach()

if (NOT FINE)
    message(FATAL_ERROR "there are unformatted files")
endif()
