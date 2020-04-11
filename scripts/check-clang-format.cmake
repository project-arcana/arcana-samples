FILE(GLOB_RECURSE files 
    "tests/*.cc"
    "tests/*.hh"
    "extern/*.cc"
    "extern/*.hh"
)

option(ONLY_CHECK "if true, only checks if anything changed, otherwise executes clang format as well" ON)

set(FINE TRUE)

foreach(filename ${files})

    # ignore sdl2
    if (filename MATCHES "extern/sdl2-dev/")
        continue()
    endif()
    
    file(READ ${filename} content)

    execute_process(
        COMMAND clang-format-7 -style=file ${filename}
        OUTPUT_VARIABLE formatted
        RESULT_VARIABLE status
    )

    if (status EQUAL 0)
        if (content STREQUAL formatted)
            # fine
        else()
            message("${filename} is not formatted")
            set(FINE FALSE)

            if (NOT ONLY_CHECK)
                message(" .. formatting ${filename}")
                
                execute_process(
                    COMMAND clang-format-7 -i -style=file ${filename}
                    RESULT_VARIABLE status
                )

                if (status EQUAL 0)
                    # fine
                else()
                    message(FATAL_ERROR "clang format -i error for ${filename}")
                endif()
            endif()
        endif()
    else()
        message(FATAL_ERROR "clang format error for ${filename}")
    endif()
endforeach()

if (NOT FINE)
    message(FATAL_ERROR "there are unformatted files")
endif()
