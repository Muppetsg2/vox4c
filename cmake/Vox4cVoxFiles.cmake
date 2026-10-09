# Helpers for targets / tests that run a vox4c executable on every .vox file
# of VOX4C_VOX_FILES_DIR (searched recursively when the target runs).

set(VOX4C_RUN_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/vox4cRunOnVoxFiles.cmake")
set(VOX4C_FILE_TIMEOUT 600 CACHE STRING "Timeout in seconds for one .vox file in the vox4c_* run targets")

# Builds the "cmake -P" command line into <out_var>.
#   _vox4c_run_command(<out_var> MODE <mode> EXECUTABLE <target>
#                      [ARGS <arg>...] [OUTPUT <path>] [INJECT <md file> INJECT_TAG <tag>] [VERBOSE])
function(_vox4c_run_command out_var)
    cmake_parse_arguments(RUN "VERBOSE" "MODE;EXECUTABLE;OUTPUT;INJECT;INJECT_TAG" "ARGS" ${ARGN})
    string(REPLACE ";" "|" _args "${RUN_ARGS}")
    set(_cmd "${CMAKE_COMMAND}"
        "-DVOX4C_MODE=${RUN_MODE}"
        "-DVOX4C_EXE=$<TARGET_FILE:${RUN_EXECUTABLE}>"
        "-DVOX4C_VOX_DIR=${VOX4C_VOX_FILES_DIR}"
        "-DVOX4C_ARGS=${_args}"
        "-DVOX4C_OUTPUT=${RUN_OUTPUT}"
        "-DVOX4C_TIMEOUT=${VOX4C_FILE_TIMEOUT}"
        "-DVOX4C_INJECT=${RUN_INJECT}"
        "-DVOX4C_INJECT_TAG=${RUN_INJECT_TAG}")
    if(RUN_VERBOSE)
        list(APPEND _cmd "-DVOX4C_VERBOSE=ON")
    endif()
    list(APPEND _cmd -P "${VOX4C_RUN_SCRIPT}")
    set(${out_var} "${_cmd}" PARENT_SCOPE)
endfunction()

# Custom target that runs <executable> on all .vox files.
#   vox4c_add_vox_files_target(<name> MODE <TEST|EACH|MEMORY|OBJ> EXECUTABLE <target>
#                              [ARGS ...] [OUTPUT <path>] [INJECT <md file> INJECT_TAG <tag>]
#                              [VERBOSE] [COMMENT <text>])
function(vox4c_add_vox_files_target name)
    cmake_parse_arguments(T "VERBOSE" "MODE;EXECUTABLE;OUTPUT;INJECT;INJECT_TAG;COMMENT" "ARGS" ${ARGN})
    set(_verbose "")
    if(T_VERBOSE)
        set(_verbose VERBOSE)
    endif()
    _vox4c_run_command(_cmd MODE ${T_MODE} EXECUTABLE ${T_EXECUTABLE} ARGS ${T_ARGS} OUTPUT "${T_OUTPUT}"
                       INJECT "${T_INJECT}" INJECT_TAG "${T_INJECT_TAG}" ${_verbose})
    if(NOT T_COMMENT)
        set(T_COMMENT "Running ${T_EXECUTABLE} on the .vox files in ${VOX4C_VOX_FILES_DIR}")
    endif()
    add_custom_target(${name}
        COMMAND ${_cmd}
        DEPENDS ${T_EXECUTABLE}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "${T_COMMENT}"
        USES_TERMINAL
        VERBATIM)
endfunction()

# CTest test that runs <executable> on all .vox files.
#   vox4c_add_vox_files_test(<name> MODE <mode> EXECUTABLE <target> [ARGS ...] [OUTPUT <path>])
function(vox4c_add_vox_files_test name)
    cmake_parse_arguments(T "" "MODE;EXECUTABLE;OUTPUT" "ARGS" ${ARGN})
    _vox4c_run_command(_cmd MODE ${T_MODE} EXECUTABLE ${T_EXECUTABLE} ARGS ${T_ARGS} OUTPUT "${T_OUTPUT}")
    add_test(NAME ${name} COMMAND ${_cmd})
endfunction()