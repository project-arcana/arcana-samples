# to be executed in arcana-samples root dir

set(TEST_BUILD_TYPE "Debug" CACHE STRING "which CMAKE_BUILD_TYPE was used for building the tests?")

if (WIN32)
    FILE(GLOB_RECURSE tests "bin/*-tests.exe")
else()
    FILE(GLOB_RECURSE tests "bin/${TEST_BUILD_TYPE}/*-tests")
endif()

SET(FINE TRUE)
SET(FAILS "")

foreach (test ${tests})
    message(STATUS "executing ${test}")

    execute_process(
        COMMAND ${test}
        WORKING_DIRECTORY "bin/"
        RESULT_VARIABLE status
    )

    if (status EQUAL 0)
        # fine
    else()
        SET(FINE FALSE)
        message("${test} failed")
        list(APPEND FAILS ${test})
    endif()
endforeach()

if (NOT FINE)
    message("================================")
    message("")
    message("================================")
    message("the following tests are failing:")
    foreach (test ${FAILS})
        message("  ${test}")
    endforeach()
    message(FATAL_ERROR "some tests are failing")
endif()
