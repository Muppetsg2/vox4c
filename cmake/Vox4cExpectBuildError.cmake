# Builds a target that must NOT compile and checks that the build fails with
# the expected message. Used by the vox4c_run_misuse_check target and CTest.
#
#   cmake -DBUILD_DIR=<build folder> -DTARGET=<target> -DCONFIG=<config>
#         -DEXPECT=<regular expression> -P vox4cExpectBuildError.cmake

foreach(_required BUILD_DIR TARGET EXPECT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "vox4cExpectBuildError: ${_required} is not set")
    endif()
endforeach()

set(_config_args "")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    set(_config_args --config "${CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TARGET}" ${_config_args}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _out)

if(_rc EQUAL 0)
    message(FATAL_ERROR "[FAIL] ${TARGET} compiled, but it must be rejected")
endif()
if(NOT _out MATCHES "${EXPECT}")
    message(FATAL_ERROR "[FAIL] ${TARGET} failed to build, but not with the expected message "
                        "'${EXPECT}':\n${_out}")
endif()
message(STATUS "[ OK ] ${TARGET} is rejected with the expected #error")