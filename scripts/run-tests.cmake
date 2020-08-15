# to be executed in arcana-samples root dir

set(TEST_BUILD_TYPE "Debug" CACHE STRING "which CMAKE_BUILD_TYPE was used for building the tests?")

if (WIN32)
    FILE(GLOB_RECURSE tests "bin/*-tests.exe")
else()
    FILE(GLOB_RECURSE tests "bin/${TEST_BUILD_TYPE}/*-tests")
endif()

if (NOT tests)
    message(FATAL_ERROR "no tests found (wrong working directory?) CMAKE_CURRENT_SOURCE_DIR is '${CMAKE_CURRENT_SOURCE_DIR}'")
endif()

set(FINE TRUE)
set(FAILED_TEST_PATHS "")
set(FAILED_TEST_OUTPUTS "")

foreach (test ${tests})
    message(STATUS "executing ${test}")

    execute_process(
        COMMAND ${test}
        WORKING_DIRECTORY "bin/"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE test_output
        ERROR_VARIABLE test_output
    )

    if (status EQUAL 0)
        # fine
    else()
        SET(FINE FALSE)
        message("-- failed ${test}")
        list(APPEND FAILED_TEST_PATHS ${test})
        list(APPEND FAILED_TEST_OUTPUTS ${test_output})
    endif()
endforeach()

if (NOT FINE)
    message("================================")
    message("")
    message("================================")
    message("the following tests are failing:")

    
    list(LENGTH FAILED_TEST_PATHS num_failed_tests)
    math(EXPR iteration_length "${num_failed_tests} - 1")
    foreach(fail_index RANGE ${iteration_length})
        list(GET FAILED_TEST_PATHS ${fail_index} failed_test_name)
        list(GET FAILED_TEST_OUTPUTS ${fail_index} failed_test_output)
        message("  ${failed_test_name}:")
        message("${failed_test_output}")
    endforeach()
    message("")
    message("================================")
    message(FATAL_ERROR "some tests are failing (run 'cmake -P scripts/run-tests.cmake' in arcana-samples root to execute this check manually)")
endif()
